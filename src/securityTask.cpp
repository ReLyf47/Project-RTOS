#include "tasks.h"
#include "config.h"
#include "security.h"
#include <Arduino.h>

void vSecurityTask(void *pvParameters) {
    BaseType_t xStatus;
    TickType_t xLastWakeTime = xTaskGetTickCount();
    static uint32_t last_log_time = 0;
    static int last_attempt_count = 0;

    xSemaphoreTake(serialMutex, pdMS_TO_TICKS(100));
    Serial.println("[Security] Task started - monitoring failed attempts...");
    xSemaphoreGive(serialMutex);

    while (1) {
        TickType_t start = xTaskGetTickCount();
        uint32_t current_time = millis();
        int current_attempts = securityState.failed_attempts;

        if (current_attempts != last_attempt_count) {
            last_attempt_count = current_attempts;
            
            xSemaphoreTake(serialMutex, pdMS_TO_TICKS(100));
            Serial.printf("[Security] Failed attempts: %d/%d\n",
                          current_attempts, MAX_FAILED_ATTEMPTS);
            xSemaphoreGive(serialMutex);

            if (current_attempts >= MAX_FAILED_ATTEMPTS - 1) {
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
            if (current_time - last_log_time > 10000) {
                last_log_time = current_time;
                
                xSemaphoreTake(serialMutex, pdMS_TO_TICKS(100));
                Serial.printf("[Security] System operational - failed attempts: %d/%d\n",
                              securityState.failed_attempts, MAX_FAILED_ATTEMPTS);
                xSemaphoreGive(serialMutex);
            }
        }

        vTaskDelayUntil(&xLastWakeTime, pdMS_TO_TICKS(SECURITY_TASK_PERIOD));
    }
}