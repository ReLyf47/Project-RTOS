#include "tasks.h"
#include "config.h"
#include <Arduino.h>
#include <LiquidCrystal_I2C.h>

// Initialize LCD
LiquidCrystal_I2C lcd(LCD_ADDR, LCD_COLS, LCD_ROWS);

void vDisplayTask(void *pvParameters) {
    EventLog eventLog;
    BaseType_t xStatus;
    TickType_t xLastWakeTime = xTaskGetTickCount();
    char lcd_line1[17] = {0};
    char lcd_line2[17] = {0};
    static uint32_t last_event_time = 0;

    // Initialize LCD
    lcd.init();
    lcd.backlight();
    lcd.print("Initializing...");
    delay(1000);
    lcd.clear();

    xSemaphoreTake(serialMutex, pdMS_TO_TICKS(100));
    Serial.println("[Display] Task started - LCD initialized");
    xSemaphoreGive(serialMutex);

    while (1) {
        // Wait for event log from queue (250ms timeout)
        xStatus = xQueueReceive(eventLogQueue, &eventLog, pdMS_TO_TICKS(250));

        if (xStatus == pdPASS) {
            TickType_t start = xTaskGetTickCount();
            // Debounce: skip if event is too recent
            uint32_t current_time = millis();
            if (current_time - last_event_time < 100) {
                continue;
            }
            last_event_time = current_time;

            // Format display based on event type
            memset(lcd_line1, 0, sizeof(lcd_line1));
            memset(lcd_line2, 0, sizeof(lcd_line2));

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
                    // FIXED: format string protection added
                    snprintf(lcd_line2, 17, "%s", eventLog.message); 
                    break;
            }

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
            // No event - show idle screen
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

        // Periodic delay 250ms
        vTaskDelayUntil(&xLastWakeTime, pdMS_TO_TICKS(DISPLAY_TASK_PERIOD));
    }
}