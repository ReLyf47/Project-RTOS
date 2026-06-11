#include "tasks.h"
#include "config.h"
#include "security.h"
#include <Arduino.h>

/**
 * @brief Task monitoring keamanan dan timer lockout.
 *
 * AuthTask menaikkan counter gagal dan dapat mengaktifkan lockout. SecurityTask
 * memonitor perubahan counter tersebut, mengirim warning ke eventLogQueue, dan
 * membuka lockout saat durasinya habis.
 */
void vSecurityTask(void *pvParameters) {
    BaseType_t xStatus;

    // DelayUntil membuat pemeriksaan berjalan tiap SECURITY_TASK_PERIOD secara
    // konsisten, bukan sekadar tidur relatif setelah pekerjaan selesai.
    TickType_t xLastWakeTime = xTaskGetTickCount();

    // Static state lokal dipertahankan antar-iterasi tanpa memakai global.
    static uint32_t last_log_time = 0;
    static int last_attempt_count = 0;

    xSemaphoreTake(serialMutex, pdMS_TO_TICKS(100));
    Serial.println("[Security] Task started - monitoring failed attempts...");
    xSemaphoreGive(serialMutex);

    while (1) {
        TickType_t start = xTaskGetTickCount();
        uint32_t current_time = millis();

        // Snapshot sederhana dari shared securityState. Karena field int/uint32_t
        // dibaca singkat, risiko tearing rendah di ESP32; untuk state kompleks,
        // gunakan mutex khusus.
        int current_attempts = securityState.failed_attempts;

        if (current_attempts != last_attempt_count) {
            last_attempt_count = current_attempts;
            
            xSemaphoreTake(serialMutex, pdMS_TO_TICKS(100));
            Serial.printf("[Security] Failed attempts: %d/%d\n",
                          current_attempts, MAX_FAILED_ATTEMPTS);
            xSemaphoreGive(serialMutex);

            if (current_attempts >= MAX_FAILED_ATTEMPTS - 1) {
                // Warning dikirim sebelum lockout penuh agar DisplayTask/operator
                // bisa melihat sistem mendekati ambang gagal.
                EventLog warningEvent;
                warningEvent.type = EVENT_ERROR;
                warningEvent.timestamp = current_time;
                warningEvent.result = 0;
                snprintf(warningEvent.message, 100, 
                         "WARNING: %d failed attempts!", current_attempts);
                xQueueSend(eventLogQueue, &warningEvent, pdMS_TO_TICKS(10));
            }
        }

        if (securityState.is_locked) {
            // Unlock berbasis waktu millis(). isSystemLocked() juga punya logika
            // auto-unlock, tetapi task ini membuat transisi tersebut terlihat di log.
            if (current_time >= securityState.lockout_until) {
                clearFailedAttempts(&securityState);

                xSemaphoreTake(serialMutex, pdMS_TO_TICKS(100));
                Serial.println("[Security] System unlocked - lockout expired");
                xSemaphoreGive(serialMutex);

                EventLog unlockEvent;
                unlockEvent.type = EVENT_SYSTEM_LOCKOUT;
                unlockEvent.timestamp = current_time;
                unlockEvent.result = 1;
                strcpy(unlockEvent.message, "System unlocked");
                xQueueSend(eventLogQueue, &unlockEvent, pdMS_TO_TICKS(10));
            } else {
                uint32_t remaining_ms = securityState.lockout_until - current_time;
                // Throttle log lockout tiap 5 detik agar Serial tidak penuh.
                if (current_time - last_log_time > 5000) {
                    last_log_time = current_time;
                    
                    TickType_t end = xTaskGetTickCount();
                    xSemaphoreTake(serialMutex, pdMS_TO_TICKS(100));
                    Serial.printf("[Security] System locked - %lu ms remaining\n", 
                                  remaining_ms);
                    xSemaphoreGive(serialMutex);
                }
            }
        } else {
            // Health log normal tiap 10 detik. Ini membantu debugging tanpa
            // menghasilkan output setiap siklus 1 detik.
            if (current_time - last_log_time > 10000) {
                last_log_time = current_time;
                
                xSemaphoreTake(serialMutex, pdMS_TO_TICKS(100));
                Serial.printf("[Security] System operational - failed attempts: %d/%d\n",
                              securityState.failed_attempts, MAX_FAILED_ATTEMPTS);
                xSemaphoreGive(serialMutex);
            }
        }

        // Perioda monitoring didefinisikan di config.h agar mudah dituning.
        vTaskDelayUntil(&xLastWakeTime, pdMS_TO_TICKS(SECURITY_TASK_PERIOD));
    }
}
