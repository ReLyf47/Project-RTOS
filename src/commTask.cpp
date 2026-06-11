#include "tasks.h"
#include "config.h"
#include "comm.h"
#include <Arduino.h>

void vCommTask(void *pvParameters) {
    TickType_t xLastWakeTime = xTaskGetTickCount();

    xSemaphoreTake(serialMutex, pdMS_TO_TICKS(100));
    Serial.println("[Comm] Task started — WiFi watchdog + heartbeat active");
    xSemaphoreGive(serialMutex);

    while (1) {
        TickType_t start = xTaskGetTickCount();
        // ── WiFi health check ─────────────────────────────────────────────
        if (!isWiFiConnected()) {
            TickType_t end = xTaskGetTickCount();
            xSemaphoreTake(serialMutex, pdMS_TO_TICKS(100));
            Serial.println("[Comm] WiFi lost — reconnecting...");
            xSemaphoreGive(serialMutex);

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

        // ── Periodic heartbeat to XAMPP ───────────────────────────────────
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

        // Period defined in config.h (default 5000 ms)
        vTaskDelayUntil(&xLastWakeTime, pdMS_TO_TICKS(COMM_TASK_PERIOD));
    }
}