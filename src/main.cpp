#include <Arduino.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include <WiFi.h>
#include <Wire.h>

#include "config.h"
#include "data_structures.h"
#include "tasks.h"
#include "security.h"
#include "comm.h"

// ============ GLOBAL FREERTOS OBJECTS ============
// Semua handle di bawah dibuat sekali saat boot dan dipakai bersama oleh task.
// Handle disimpan global karena FreeRTOS object bersifat kernel-level object:
// task yang berbeda hanya perlu memegang handle, bukan salinan object-nya.

// Queue data RFID: jalur producer-consumer dari InputTask ke AuthTask.
// Queue membuat pembacaan kartu tidak hilang walaupun AuthTask sedang HTTP.
QueueHandle_t rfidDataQueue  = NULL;

// Queue event sistem: jalur dari Auth/Security task menuju DisplayTask.
// DisplayTask menjadi satu-satunya konsumen LCD sehingga output lebih rapi.
QueueHandle_t eventLogQueue  = NULL;

// Binary semaphore untuk mengubah edge interrupt RFID menjadi kerja task.
// ISR memberi semaphore, InputTask mengambilnya di context task yang aman.
SemaphoreHandle_t rfidReadSemaphore     = NULL;

// Binary semaphore penanda WiFi pernah berhasil tersambung saat boot.
// Saat ini dipakai sebagai state signal yang tersedia untuk ekstensi task lain.
SemaphoreHandle_t wifiConnectedSemaphore = NULL;

// Mutex untuk melindungi Serial dari output task yang saling bertumpuk.
// Beberapa akses LCD juga digabung dengan mutex ini karena keduanya I/O lambat.
SemaphoreHandle_t serialMutex = NULL;

// State keamanan dibagi antara AuthTask dan SecurityTask.
// Catatan: aksesnya sederhana dan singkat; bila diperluas, pertimbangkan mutex.
SecurityState securityState;

// ============ ISR ============

/**
 * @brief ISR RFID yang sangat pendek dan aman untuk interrupt context.
 *
 * ISR tidak membaca RFID, tidak melakukan Serial print, dan tidak memanggil
 * API blocking. Ia hanya memberi semaphore ke InputTask. Pola ini menjaga
 * latency interrupt tetap kecil dan memindahkan pekerjaan berat ke scheduler.
 */
void IRAM_ATTR rfidISR() {
    // Flag ini diisi oleh FreeRTOS bila task yang dibangunkan memiliki prioritas
    // lebih tinggi dari task yang sedang berjalan sebelum interrupt terjadi.
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;

    // Versi FromISR wajib dipakai di ISR; versi normal xSemaphoreGive() tidak
    // aman karena dapat menyentuh mekanisme blocking milik task context.
    xSemaphoreGiveFromISR(rfidReadSemaphore, &xHigherPriorityTaskWoken);

    // Jika InputTask perlu segera berjalan, minta context switch saat ISR selesai.
    portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
}

// ============ INITIALIZATION ============

/**
 * @brief Membuat queue, semaphore, dan mutex yang menjadi fondasi sinkronisasi.
 *
 * Fungsi ini harus dipanggil sebelum attachInterrupt() dan sebelum task dibuat,
 * supaya ISR/task tidak pernah memakai handle NULL. Ukuran queue diambil dari
 * config.h agar kapasitas buffering dapat diatur tanpa menyentuh logika task.
 */
void initializeSynchronization() {
    // Queue menyimpan salinan struct, bukan pointer, sehingga payload tetap valid
    // walaupun variabel lokal producer sudah keluar scope.
    rfidDataQueue  = xQueueCreate(RFID_DATA_QUEUE_SIZE, sizeof(RFIDData));
    eventLogQueue  = xQueueCreate(EVENT_LOG_QUEUE_SIZE,  sizeof(EventLog));

    // Binary semaphore default-nya kosong; InputTask akan menunggu sampai ISR
    // memberi token saat RFID_INT_PIN mendeteksi edge.
    rfidReadSemaphore     = xSemaphoreCreateBinary();
    wifiConnectedSemaphore = xSemaphoreCreateBinary();

    // Mutex menyediakan priority inheritance; lebih cocok dari binary semaphore
    // untuk resource bersama seperti Serial.
    serialMutex           = xSemaphoreCreateMutex();

    if (!rfidDataQueue || !eventLogQueue ||
        !rfidReadSemaphore || !serialMutex) {
        Serial.println("[ERROR] Failed to create FreeRTOS objects!");
        // Stop boot dengan delay RTOS agar watchdog/scheduler masih punya waktu.
        while (1) vTaskDelay(pdMS_TO_TICKS(100));
    }
}

/**
 * @brief Inisialisasi I/O fisik ESP32: Serial, GPIO output, input interrupt, I2C.
 *
 * Semua pin output dipaksa LOW sebelum task dibuat agar relay/LED/buzzer tidak
 * berada di kondisi acak saat scheduler mulai menjalankan task.
 */
void setupHardware() {
    Serial.begin(115200);
    delay(500);
    Serial.println("\n\n=== FreeRTOS RFID Attendance System (XAMPP backend) ===\n");

    pinMode(BUZZER_PIN, OUTPUT);
    pinMode(GREEN_LED,  OUTPUT);
    pinMode(RED_LED,    OUTPUT);
    pinMode(RELAY_PIN,  OUTPUT);
    //pinMode(RFID_INT_PIN, INPUT);

    digitalWrite(BUZZER_PIN, LOW);
    digitalWrite(GREEN_LED,  LOW);
    digitalWrite(RED_LED,    LOW);
    digitalWrite(RELAY_PIN,  LOW);
    ledcAttachPin(BUZZER_PIN, 0);  // pin, channel
    ledcSetup(0, 1000, 8);         // channel, freq, resolution

    Wire.begin(21, 22);   // SDA=21, SCL=22
    Serial.println("[BOOT] Hardware initialized");
}

/**
 * @brief Koneksi WiFi awal sebelum task komunikasi mengambil alih monitoring.
 *
 * Koneksi boot dibuat blocking dengan batas 20 percobaan. Setelah scheduler
 * berjalan, CommTask bertugas melakukan reconnect dan heartbeat secara periodik.
 */
void setupWiFi() {
    Serial.printf("[BOOT] Connecting to WiFi: %s\n", WIFI_SSID);

    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 20) {
        delay(500);
        Serial.print(".");
        attempts++;
    }
    Serial.println();

    if (WiFi.status() == WL_CONNECTED) {
        // Sinyal ini dapat dipakai task lain sebagai gate awal konektivitas.
        xSemaphoreGive(wifiConnectedSemaphore);
        Serial.printf("[BOOT] WiFi connected — IP: %s\n", WiFi.localIP().toString().c_str());
        Serial.printf("[BOOT] XAMPP server: %s\n", XAMPP_BASE_URL);
    } else {
        Serial.println("[BOOT] WARNING: WiFi not connected — auth will fail until reconnected");
    }
}

/**
 * @brief Membuat seluruh task aplikasi dengan stack dan prioritas dari config.h.
 *
 * Prioritas tinggi diberikan ke InputTask/AuthTask karena keduanya berada di
 * jalur akses kartu. Comm/Security berjalan medium, Display low karena output
 * LCD tidak boleh menghambat autentikasi.
 */
void createAllTasks() {
    xTaskCreate(vInputTask,    "InputTask",    INPUT_TASK_STACK,    NULL, INPUT_PRIORITY,    NULL);
    xTaskCreate(vAuthTask,     "AuthTask",     AUTH_TASK_STACK,     NULL, AUTH_PRIORITY,     NULL);
    xTaskCreate(vCommTask,     "CommTask",     COMM_TASK_STACK,     NULL, COMM_PRIORITY,     NULL);
    xTaskCreate(vSecurityTask, "SecurityTask", SECURITY_TASK_STACK, NULL, SECURITY_PRIORITY, NULL);
    xTaskCreate(vDisplayTask,  "DisplayTask",  DISPLAY_TASK_STACK,  NULL, DISPLAY_PRIORITY,  NULL);

    Serial.println("[BOOT] All 5 FreeRTOS tasks created");
}

// ============ MAIN ============

void setup() {
    // Urutan boot penting: hardware dulu, lalu object sinkronisasi, baru ISR/task.
    setupHardware();
    initializeSynchronization();

    // Zero out security state agar lockout/attempt counter mulai dari kondisi
    // deterministik walaupun RAM menyimpan nilai sisa reset sebelumnya.
    memset(&securityState, 0, sizeof(SecurityState));

    setupWiFi();

    // Interrupt dipasang setelah semaphore dibuat. Mode FALLING mengasumsikan
    // modul RFID menarik pin INT ke LOW saat kartu terdeteksi.
    attachInterrupt(digitalPinToInterrupt(RFID_INT_PIN), rfidISR, FALLING);
    Serial.printf("[BOOT] ISR attached to GPIO %d\n", RFID_INT_PIN);

    createAllTasks();
}

void loop() {
    // Arduino loop tetap berjalan sebagai task bawaan. Delay RTOS dipakai agar
    // task lain tetap mendapatkan CPU, bukan busy-wait dengan delay blocking lama.
    vTaskDelay(pdMS_TO_TICKS(1000));

    static int counter = 0;
    if (++counter >= 10) {
        counter = 0;
        printTaskStats();
    }
}

// ============ UTILITY ============

/**
 * @brief Snapshot status sistem untuk debugging periodik.
 *
 * Output Serial dilindungi mutex supaya baris statistik tidak bercampur dengan
 * log dari task lain. Timeout pendek mencegah task ini menunggu terlalu lama.
 */
void printTaskStats() {
    xSemaphoreTake(serialMutex, pdMS_TO_TICKS(100));
    Serial.println("\n=== SYSTEM STATISTICS ===");
    Serial.printf("WiFi     : %s  (%d dBm)\n",
                  isWiFiConnected() ? "Connected" : "Disconnected",
                  getWiFiSignalStrength());
    Serial.printf("Locked   : %s\n", isSystemLocked(&securityState) ? "YES" : "no");
    Serial.printf("Attempts : %d / %d\n",
                  securityState.failed_attempts, MAX_FAILED_ATTEMPTS);
    Serial.println("=========================\n");
    xSemaphoreGive(serialMutex);
}
