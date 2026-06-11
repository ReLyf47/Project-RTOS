#include "tasks.h"
#include "config.h"
#include <Arduino.h>
#include <LiquidCrystal_I2C.h>

// Object LCD global karena driver I2C menyimpan state perangkat dan dipakai
// sepanjang hidup DisplayTask.
LiquidCrystal_I2C lcd(LCD_ADDR, LCD_COLS, LCD_ROWS);

/**
 * @brief Task tampilan: konsumen eventLogQueue dan pengendali LCD 16x2.
 *
 * Task ini dijalankan low priority karena LCD/Serial adalah I/O lambat dan tidak
 * boleh mengalahkan jalur pembacaan/autentikasi kartu. Semua event diproses dari
 * queue agar task lain tidak menulis LCD secara langsung.
 */
void vDisplayTask(void *pvParameters) {
    EventLog eventLog;
    BaseType_t xStatus;

    // Basis periodik untuk refresh idle screen dan polling queue.
    TickType_t xLastWakeTime = xTaskGetTickCount();

    // Buffer 17 byte: 16 karakter LCD + null terminator.
    char lcd_line1[17] = {0};
    char lcd_line2[17] = {0};
    static uint32_t last_event_time = 0;

    // Inisialisasi LCD dilakukan di task ini agar ownership I2C/LCD jelas.
    lcd.init();
    lcd.backlight();
    lcd.print("Initializing...");
    delay(1000);
    lcd.clear();

    xSemaphoreTake(serialMutex, pdMS_TO_TICKS(100));
    Serial.println("[Display] Task started - LCD initialized");
    xSemaphoreGive(serialMutex);

    while (1) {
        // Tunggu event dari Auth/Security task. Timeout memungkinkan task tetap
        // memperbarui idle screen walaupun queue kosong.
        xStatus = xQueueReceive(eventLogQueue, &eventLog, pdMS_TO_TICKS(250));

        if (xStatus == pdPASS) {
            // Debounce event tampilan: mencegah LCD berkedip terlalu cepat jika
            // beberapa event masuk hampir bersamaan.
            TickType_t start = xTaskGetTickCount();
            // Debounce: skip if event is too recent
            uint32_t current_time = millis();
            if (current_time - last_event_time < 100) {
                continue;
            }
            last_event_time = current_time;

            // Bersihkan buffer sebelum snprintf supaya sisa string sebelumnya
            // tidak tertinggal di layar bila pesan baru lebih pendek.
            memset(lcd_line1, 0, sizeof(lcd_line1));
            memset(lcd_line2, 0, sizeof(lcd_line2));

            // Mapping EventType ke dua baris LCD. snprintf membatasi output ke
            // 16 karakter plus terminator sesuai ukuran LCD.
            switch (eventLog.type) {
                case EVENT_ACCESS_GRANTED:
                    snprintf(lcd_line1, 17, "ACCESS GRANTED");
                    snprintf(lcd_line2, 17, "UID:%02X%02X%02X%02X",
                             eventLog.uid[0], eventLog.uid[1],
                             eventLog.uid[2], eventLog.uid[3]);
                    break;

                case EVENT_ACCESS_DENIED:
                case EVENT_SPOOFING_DETECTED:
                    snprintf(lcd_line1, 17, "ACCESS DENIED");
                    snprintf(lcd_line2, 17, "INVALID CARD");
                    break;

                case EVENT_SYSTEM_LOCKOUT:
                    snprintf(lcd_line1, 17, "SYSTEM LOCKED");
                    snprintf(lcd_line2, 17, "Try again later");
                    break;

                case EVENT_UID_REGISTERED:
                    snprintf(lcd_line1, 17, "UID REGISTERED");
                    snprintf(lcd_line2, 17, "Saved!");
                    break;

                case EVENT_DATABASE_SYNCED:
                    snprintf(lcd_line1, 17, "DB SYNCHRONIZED");
                    snprintf(lcd_line2, 17, "Cloud updated");
                    break;

                default:
                    snprintf(lcd_line1, 17, "STATUS: %d", eventLog.type);
                    // Format string protection: pesan diperlakukan sebagai data,
                    // bukan sebagai format printf.
                    snprintf(lcd_line2, 17, "%s", eventLog.message); 
                    break;
            }

            // Update LCD dan Serial di satu critical section agar output event
            // tampil konsisten. Timeout pendek mencegah deadlock panjang.
            // Update LCD with mutex protection
            TickType_t end = xTaskGetTickCount();
            xSemaphoreTake(serialMutex, pdMS_TO_TICKS(100));
            
            lcd.clear();
            lcd.setCursor(0, 0);
            lcd.print(lcd_line1);
            lcd.setCursor(0, 1);
            lcd.print(lcd_line2);

            // Also print to Serial
            Serial.printf("[Display] Event: %s\n", eventLog.message);
            Serial.printf("           User: %s\n", eventLog.user_name);
            
            xSemaphoreGive(serialMutex);
        } else {
            // Queue kosong: tampilkan idle screen kira-kira tiap 1 detik
            // (4 siklus x DISPLAY_TASK_PERIOD 250 ms).
            static uint32_t idle_counter = 0;
            if (++idle_counter > 4) {  // Every 1 second
                idle_counter = 0;
                
                xSemaphoreTake(serialMutex, pdMS_TO_TICKS(100));
                
                lcd.clear();
                lcd.setCursor(0, 0);
                lcd.print("Ready to scan");
                lcd.setCursor(0, 1);
                lcd.print("Tap your card");
                
                xSemaphoreGive(serialMutex);
            }
        }

        // Delay periodik menjaga refresh display tidak memonopoli CPU.
        vTaskDelayUntil(&xLastWakeTime, pdMS_TO_TICKS(DISPLAY_TASK_PERIOD));
    }
}
