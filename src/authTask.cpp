#include "tasks.h"
#include "config.h"
#include "security.h"
#include "comm.h"
#include <Arduino.h>

void vAuthTask(void *pvParameters) {
    RFIDData         rfidData;
    EventLog         eventLog;
    ServerAuthResponse authResp;
    TickType_t       xLastWakeTime = xTaskGetTickCount();

    xSemaphoreTake(serialMutex, pdMS_TO_TICKS(100));
    Serial.println("[Auth] Task started — verifying UIDs via XAMPP server");
    xSemaphoreGive(serialMutex);

    while (1) {
        // Block until a UID arrives (100 ms timeout so vTaskDelayUntil still fires)
        if (xQueueReceive(rfidDataQueue, &rfidData, pdMS_TO_TICKS(100)) == pdPASS) {
            TickType_t start = xTaskGetTickCount();
            // Reject scans while system is locked out
            if (isSystemLocked(&securityState)) {
                feedbackLockout();

                eventLog.type      = EVENT_SYSTEM_LOCKOUT;
                eventLog.timestamp = rfidData.timestamp;
                eventLog.result    = 0;
                uidCopy(rfidData.uid, eventLog.uid);
                strcpy(eventLog.user_name, "Unknown");
                strcpy(eventLog.message, "Scan rejected — system locked");
                xQueueSend(eventLogQueue, &eventLog, pdMS_TO_TICKS(10));

                xSemaphoreTake(serialMutex, pdMS_TO_TICKS(100));
                Serial.println("[Auth] Scan rejected — system is locked");
                xSemaphoreGive(serialMutex);

                vTaskDelayUntil(&xLastWakeTime, pdMS_TO_TICKS(AUTH_TASK_PERIOD));
                continue;
            }

            // Convert scanned UID to hex string for HTTP calls
            char uid_hex[9];
            uidBinaryToHex(rfidData.uid, uid_hex);

            xSemaphoreTake(serialMutex, pdMS_TO_TICKS(100));
            Serial.printf("[Auth] Verifying UID %s with server...\n", uid_hex);
            xSemaphoreGive(serialMutex);

            // ── Step 1: Verify with XAMPP ──────────────────────────────────
            int net_ok = verifyUIDWithServer(uid_hex, &authResp);

            if (net_ok != 0) {
                // Network / server unreachable
                feedbackFailure();

                eventLog.type      = EVENT_SERVER_ERROR;
                eventLog.timestamp = rfidData.timestamp;
                eventLog.result    = 0;
                uidCopy(rfidData.uid, eventLog.uid);
                strcpy(eventLog.user_name, "Unknown");
                strcpy(eventLog.message, "Server unreachable");
                xQueueSend(eventLogQueue, &eventLog, pdMS_TO_TICKS(10));

                TickType_t end = xTaskGetTickCount();
                xSemaphoreTake(serialMutex, pdMS_TO_TICKS(100));
                Serial.printf("[Auth] Execution time: %lu ms\n", (end - start) * portTICK_PERIOD_MS);
                xSemaphoreGive(serialMutex);

                vTaskDelayUntil(&xLastWakeTime, pdMS_TO_TICKS(AUTH_TASK_PERIOD));
                continue;
            }

            if (authResp.authorized) {
                // ── Step 2: Compute next rolling token ────────────────────
                uint8_t new_uid[4];
                if (calculateRollingToken(rfidData.uid, new_uid) != 0) {
                    // Should never happen, but handle gracefully
                    feedbackFailure();
                    xSemaphoreTake(serialMutex, pdMS_TO_TICKS(100));
                    Serial.println("[Auth] ERROR: Rolling token calculation failed");
                    xSemaphoreGive(serialMutex);
                    vTaskDelayUntil(&xLastWakeTime, pdMS_TO_TICKS(AUTH_TASK_PERIOD));
                    continue;
                }

                char new_uid_hex[9];
                uidBinaryToHex(new_uid, new_uid_hex);

                // ── Step 3: Push new UID to server ────────────────────────
                if (updateUIDOnServer(authResp.user_id, new_uid_hex) != 0) {
                    // Access still granted; rolling update failed — log warning only
                    xSemaphoreTake(serialMutex, pdMS_TO_TICKS(100));
                    Serial.println("[Auth] WARNING: Rolling token update failed on server");
                    xSemaphoreGive(serialMutex);
                }

                // ── Step 4: Hardware feedback + display event ─────────────
                feedbackSuccess();
                clearFailedAttempts(&securityState);

                memset(&eventLog, 0, sizeof(EventLog));
                eventLog.type      = EVENT_ACCESS_GRANTED;
                eventLog.timestamp = rfidData.timestamp;
                eventLog.result    = 1;
                uidCopy(rfidData.uid, eventLog.uid);
                strlcpy(eventLog.user_name, authResp.user_name, sizeof(eventLog.user_name));
                snprintf(eventLog.message, sizeof(eventLog.message),
                         "Access granted — token updated to %s", new_uid_hex);
                xQueueSend(eventLogQueue, &eventLog, pdMS_TO_TICKS(10));

                // ── Step 5: Log event to server (best-effort) ─────────────
                logEventToServer(&eventLog);

                xSemaphoreTake(serialMutex, pdMS_TO_TICKS(100));
                Serial.printf("[Auth] ✓ ACCESS GRANTED: %s\n", authResp.user_name);
                Serial.printf("      UID: %s  →  %s\n", uid_hex, new_uid_hex);
                xSemaphoreGive(serialMutex);

            } else {
                // ── Access denied ─────────────────────────────────────────
                feedbackFailure();
                int attempt_count = recordFailedAttempt(&securityState);

                memset(&eventLog, 0, sizeof(EventLog));
                eventLog.type      = EVENT_SPOOFING_DETECTED;
                eventLog.timestamp = rfidData.timestamp;
                eventLog.result    = 0;
                uidCopy(rfidData.uid, eventLog.uid);
                strcpy(eventLog.user_name, "Unknown");
                snprintf(eventLog.message, sizeof(eventLog.message),
                         "UID not recognised (attempt %d/%d)",
                         attempt_count, MAX_FAILED_ATTEMPTS);
                xQueueSend(eventLogQueue, &eventLog, pdMS_TO_TICKS(10));

                logEventToServer(&eventLog);

                xSemaphoreTake(serialMutex, pdMS_TO_TICKS(100));
                Serial.printf("[Auth] ✗ ACCESS DENIED: UID %s not found\n", uid_hex);
                Serial.printf("      Failed attempt: %d/%d\n",
                              attempt_count, MAX_FAILED_ATTEMPTS);
                xSemaphoreGive(serialMutex);

                if (isSystemLocked(&securityState)) {
                    feedbackLockout();

                    EventLog lockEvent = {};
                    lockEvent.type      = EVENT_SYSTEM_LOCKOUT;
                    lockEvent.timestamp = millis();
                    lockEvent.result    = 0;
                    strcpy(lockEvent.message, "System locked — too many failed attempts");
                    xQueueSend(eventLogQueue, &lockEvent, pdMS_TO_TICKS(10));
                    logEventToServer(&lockEvent);

                    xSemaphoreTake(serialMutex, pdMS_TO_TICKS(100));
                    Serial.println("[Auth] ⚠ SYSTEM LOCKED!");
                    xSemaphoreGive(serialMutex);
                }
            }
        }

        vTaskDelayUntil(&xLastWakeTime, pdMS_TO_TICKS(AUTH_TASK_PERIOD));
    }
}