#include "tasks.h"
#include "config.h"
#include "security.h"
#include "comm.h"
#include <Arduino.h>

/**
 * @brief Task autentikasi utama untuk UID RFID.
 *
 * Task ini adalah konsumen rfidDataQueue dan producer eventLogQueue. Ia juga
 * melakukan I/O jaringan ke XAMPP, menghitung rolling token, mengubah state
 * lockout, dan memberi feedback hardware.
 *
 * Catatan scheduling:
 * - Queue receive memakai timeout 100 ms supaya task tetap punya titik periodik.
 * - AUTH_TASK_STACK lebih besar karena HTTPClient dan ArduinoJson memakai stack
 *   lebih banyak dibanding task GPIO biasa.
 */
void vAuthTask(void *pvParameters) {
    // Payload RFID dari queue input. QueueReceive menyalin isi struct ke sini.
    RFIDData         rfidData;

    // Event yang dikirim ke DisplayTask dan juga dapat dikirim ke server.
    EventLog         eventLog;

    // Response server disediakan caller agar httpClient tidak memakai global state.
    ServerAuthResponse authResp;

    // Basis delay periodik untuk mengurangi jitter eksekusi task.
    TickType_t       xLastWakeTime = xTaskGetTickCount();

    xSemaphoreTake(serialMutex, pdMS_TO_TICKS(100));
    Serial.println("[Auth] Task started — verifying UIDs via XAMPP server");
    xSemaphoreGive(serialMutex);

    while (1) {
        // Blok sampai ada UID, tetapi tetap timeout agar lock/scheduling periodik
        // tidak bergantung pada ada/tidaknya kartu.
        if (xQueueReceive(rfidDataQueue, &rfidData, pdMS_TO_TICKS(100)) == pdPASS) {

            // Lockout dicek paling awal supaya kartu apa pun ditolak tanpa HTTP.
            // Ini mengurangi traffic server saat ada percobaan spoofing beruntun.
            TickType_t start = xTaskGetTickCount();
            // Reject scans while system is locked out
            if (isSystemLocked(&securityState)) {
                feedbackLockout();

                // Event lockout dikirim ke DisplayTask. Payload UID tetap dicatat
                // agar operator tahu kartu mana yang mencoba saat sistem terkunci.
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

            // Server XAMPP menerima UID sebagai string 8 hex, sedangkan reader
            // dan queue firmware menyimpan UID sebagai 4 byte.
            char uid_hex[9];
            uidBinaryToHex(rfidData.uid, uid_hex);

            xSemaphoreTake(serialMutex, pdMS_TO_TICKS(100));
            Serial.printf("[Auth] Verifying UID %s with server...\n", uid_hex);
            xSemaphoreGive(serialMutex);

            // Step 1: verifikasi UID dengan database XAMPP lewat HTTP POST.
            // Return 0 berarti HTTP/JSON sukses; authorized menentukan akses.
            int net_ok = verifyUIDWithServer(uid_hex, &authResp);

            if (net_ok != 0) {
                // Network/server unreachable: bedakan dari kartu tidak terdaftar.
                // Failed attempt tidak dinaikkan karena ini bukan bukti spoofing.
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
                // Step 2: hitung UID berikutnya untuk rolling token.
                // Rolling token mengurangi risiko replay UID lama.
                uint8_t new_uid[4];
                if (calculateRollingToken(rfidData.uid, new_uid) != 0) {
                    // Harusnya tidak terjadi karena buffer valid, tetapi tetap
                    // ditangani agar task tidak crash jika kontrak fungsi berubah.
                    feedbackFailure();
                    xSemaphoreTake(serialMutex, pdMS_TO_TICKS(100));
                    Serial.println("[Auth] ERROR: Rolling token calculation failed");
                    xSemaphoreGive(serialMutex);
                    vTaskDelayUntil(&xLastWakeTime, pdMS_TO_TICKS(AUTH_TASK_PERIOD));
                    continue;
                }

                char new_uid_hex[9];
                uidBinaryToHex(new_uid, new_uid_hex);

                // Step 3: simpan UID baru ke server sebelum scan berikutnya.
                // Bila gagal, akses tetap diberi karena user sudah terverifikasi.
                if (updateUIDOnServer(authResp.user_id, new_uid_hex) != 0) {
                    xSemaphoreTake(serialMutex, pdMS_TO_TICKS(100));
                    Serial.println("[Auth] WARNING: Rolling token update failed on server");
                    xSemaphoreGive(serialMutex);
                }

                // Step 4: feedback lokal dan reset counter gagal.
                feedbackSuccess();
                clearFailedAttempts(&securityState);

                // EventLog di-clear agar field yang tidak diisi eksplisit tidak
                // membawa sisa data dari event sebelumnya.
                memset(&eventLog, 0, sizeof(EventLog));
                eventLog.type      = EVENT_ACCESS_GRANTED;
                eventLog.timestamp = rfidData.timestamp;
                eventLog.result    = 1;
                uidCopy(rfidData.uid, eventLog.uid);
                strlcpy(eventLog.user_name, authResp.user_name, sizeof(eventLog.user_name));
                snprintf(eventLog.message, sizeof(eventLog.message),
                         "Access granted — token updated to %s", new_uid_hex);
                xQueueSend(eventLogQueue, &eventLog, pdMS_TO_TICKS(10));

                // Step 5: logging server best-effort. Kegagalan log tidak boleh
                // membatalkan akses atau memblokir task terlalu lama.
                logEventToServer(&eventLog);

                xSemaphoreTake(serialMutex, pdMS_TO_TICKS(100));
                Serial.printf("[Auth] ✓ ACCESS GRANTED: %s\n", authResp.user_name);
                Serial.printf("      UID: %s  →  %s\n", uid_hex, new_uid_hex);
                xSemaphoreGive(serialMutex);

            } else {
                // Access denied: kartu tidak dikenal dianggap percobaan gagal dan
                // dapat memicu lockout setelah mencapai MAX_FAILED_ATTEMPTS.
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
                    // Lockout baru terjadi setelah recordFailedAttempt() menaikkan
                    // counter sampai ambang batas. Event ini memudahkan LCD/server
                    // membedakan denied biasa dari kondisi terkunci.
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
