#ifndef CONFIG_H
#define CONFIG_H

#include <stdint.h>

// ============ WIFI CONFIGURATION ============
// Credential access point lokal. Ganti placeholder sebelum upload firmware.
#define WIFI_SSID           "PLACEHOLDER"         // TODO: Update
#define WIFI_PASSWORD       "PLACEHOLDER"      // TODO: Update

// Batas waktu konseptual koneksi WiFi. Implementasi initWiFi saat ini memakai
// 20 percobaan x 500 ms, sehingga nilainya sejalan dengan 10000 ms.
#define WIFI_TIMEOUT_MS     10000

// ============ XAMPP SERVER CONFIGURATION ============
// Set this to your PC's local IP address (check via ipconfig / ifconfig)
// ESP32 dan PC XAMPP harus berada di jaringan yang sama.
#define XAMPP_SERVER_IP     "192.168.1.100"   // TODO: Update to your PC's IP
#define XAMPP_SERVER_PORT   80
#define XAMPP_BASE_URL      "http://10.46.30.245/rfid"

// ============ GPIO PIN DEFINITIONS ============
// Output aktuator/indikator. RELAY_PIN disiapkan untuk door lock/solenoid.
#define BUZZER_PIN          14
#define GREEN_LED           25
#define RED_LED             26
#define RELAY_PIN           27

// Input interrupt dari modul RFID; dipakai attachInterrupt(..., FALLING).
#define RFID_INT_PIN        4

// ============ I2C LCD CONFIGURATION ============
// LCD 16x2 via backpack I2C umum berada di alamat 0x27.
#define LCD_ADDR            0x27
#define LCD_COLS            16
#define LCD_ROWS            2

// ============ FREERTOS TASK PERIODS (ms) ============
// Perioda loop masing-masing task. Nilai ini dikonversi ke tick dengan
// pdMS_TO_TICKS() pada implementasi task.
#define INPUT_TASK_PERIOD   50
#define AUTH_TASK_PERIOD    100
#define COMM_TASK_PERIOD    5000
#define SECURITY_TASK_PERIOD 1000
#define DISPLAY_TASK_PERIOD 250

// ============ FREERTOS TASK STACK SIZES (bytes) ============
// Stack harus cukup untuk variabel lokal, library call, dan nested function call.
// Auth/Comm lebih besar karena HTTPClient/ArduinoJson membutuhkan stack ekstra.
#define INPUT_TASK_STACK    2048
#define AUTH_TASK_STACK     6144    // Larger: handles HTTP client calls
#define COMM_TASK_STACK     4096
#define SECURITY_TASK_STACK 2048
#define DISPLAY_TASK_STACK  2048

// ============ FREERTOS TASK PRIORITIES ============
// Angka lebih besar berarti prioritas lebih tinggi. Input/Auth dibuat tinggi
// agar scan kartu cepat ditangani; Display rendah karena hanya UI.
#define INPUT_PRIORITY      3
#define AUTH_PRIORITY       3
#define COMM_PRIORITY       2
#define SECURITY_PRIORITY   2
#define DISPLAY_PRIORITY    1

// ============ QUEUE SIZES ============
// Kapasitas queue adalah jumlah item, bukan byte. Tiap item disalin ke queue.
#define RFID_DATA_QUEUE_SIZE    5
#define EVENT_LOG_QUEUE_SIZE    10

// ============ ROLLING TOKEN CONFIGURATION ============
// Step dan mask menentukan ruang suffix 12 bit pada UID rolling token.
#define ROLLING_TOKEN_STEP  1
#define UID_MASK_LAST_3     0xFFF

// ============ SECURITY CONFIGURATION ============
// Setelah percobaan gagal mencapai ambang, sistem menolak scan sampai durasi
// lockout berakhir.
#define MAX_FAILED_ATTEMPTS 3
#define LOCKOUT_DURATION_MS 30000

// API Endpoints (PHP scripts in htdocs/rfid/api/)
// Endpoint ini dipanggil oleh httpClient.cpp dari AuthTask dan CommTask.
#define API_VERIFY_UID      XAMPP_BASE_URL "/api/verify.php"
#define API_UPDATE_UID      XAMPP_BASE_URL "/api/update_uid.php"
#define API_LOG_EVENT       XAMPP_BASE_URL "/api/log_event.php"
#define API_HEARTBEAT       XAMPP_BASE_URL "/api/heartbeat.php"

// Timeout HTTP mencegah task jaringan tertahan terlalu lama oleh server lambat.
#define HTTP_TIMEOUT_MS     5000    // Max wait for server response

// ============ LED FEEDBACK TIMINGS (ms) ============
// Durasi feedback lokal. Nilai kecil menjaga task tidak blocking lama.
#define LED_SUCCESS_PULSE   50
#define LED_FAIL_PULSE      200
#define BUZZER_SUCCESS_MS   100
#define BUZZER_FAIL_MS      300

// ============ DEBUG LOGGING ============
// Flag compile-time untuk ekstensi logging; saat ini log Serial masih langsung.
#define DEBUG_SERIAL        1

#endif // CONFIG_H
