#include "tasks.h"
#include "config.h"
#include "comm.h"
#include <Arduino.h>

/**
 * @brief Task komunikasi periodik: watchdog WiFi dan heartbeat ke XAMPP.
 *
 * Task ini sengaja dipisah dari AuthTask. AuthTask tetap fokus pada scan kartu,
 * sedangkan CommTask menjaga konektivitas dan memberi status berkala ke server.
 */
void vCommTask(void *pvParameters) {
    // Basis waktu periodik 5 detik (default) untuk health check dan heartbeat.
    TickType_t xLastWakeTime = xTaskGetTickCount();

    xSemaphoreTake(serialMutex, pdMS_TO_TICKS(100));
    Serial.println("[Comm] Task started — WiFi watchdog + heartbeat active");
    xSemaphoreGive(serialMutex);

    while (1) {
        // WiFi health check. Jika koneksi putus, reconnect dilakukan di task ini
        // agar task autentikasi tidak harus mengurus state jaringan global.
        TickType_t start = xTaskGetTickCount();
        // ── WiFi health check ─────────────────────────────────────────────
        if (!isWiFiConnected()) {
            TickType_t end = xTaskGetTickCount();
            xSemaphoreTake(serialMutex, pdMS_TO_TICKS(100));
            Serial.println("[Comm] WiFi lost — reconnecting...");
            xSemaphoreGive(serialMutex);

            // initWiFi() blocking dengan timeout internal, tetapi hanya dipanggil
            // pada kondisi putus dan dari task medium priority.
            initWiFi(WIFI_SSID, WIFI_PASSWORD);

            if (isWiFiConnected()) {
                xSemaphoreTake(serialMutex, pdMS_TO_TICKS(100));
                Serial.printf("[Comm] WiFi reconnected — IP: %s\n", getESP32IPAddress());
                xSemaphoreGive(serialMutex);
            } else {
                xSemaphoreTake(serialMutex, pdMS_TO_TICKS(100));
                Serial.println("[Comm] WARNING: WiFi reconnect failed");
                xSemaphoreGive(serialMutex);
            }
        }

        // Heartbeat periodik ke XAMPP. Ini membuktikan jalur HTTP server masih
        // hidup walaupun tidak ada kartu yang sedang discan.
        if (isWiFiConnected()) {
            if (sendHeartbeat() == 0) {
                xSemaphoreTake(serialMutex, pdMS_TO_TICKS(100));
                Serial.printf("[Comm] Heartbeat OK  RSSI: %d dBm  IP: %s\n",
                              getWiFiSignalStrength(), getESP32IPAddress());
                xSemaphoreGive(serialMutex);
            } else {
                xSemaphoreTake(serialMutex, pdMS_TO_TICKS(100));
                Serial.println("[Comm] WARNING: XAMPP server not responding");
                xSemaphoreGive(serialMutex);
            }
        }

        // Period defined in config.h (default 5000 ms). DelayUntil membatasi
        // drift sehingga heartbeat tidak makin mundur dari waktu ke waktu.
        vTaskDelayUntil(&xLastWakeTime, pdMS_TO_TICKS(COMM_TASK_PERIOD));
    }
}
