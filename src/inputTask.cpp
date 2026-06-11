#include "tasks.h"
#include "config.h"
#include "security.h"
#include <Arduino.h>
#include <SPI.h>
#include <MFRC522.h>

// Pin SPI MFRC522
#define SS_PIN   5    // SDA/CS
#define RST_PIN  4    // RST

MFRC522 mfrc522(SS_PIN, RST_PIN);

void vInputTask(void *pvParameters) {
    RFIDData rfidData;
    BaseType_t xStatus;
    TickType_t xLastWakeTime = xTaskGetTickCount();

    // Init SPI & MFRC522
    SPI.begin();
    mfrc522.PCD_Init();

    // Aktifkan IRQ pin MFRC522
    mfrc522.PCD_WriteRegister(mfrc522.ComIEnReg, 0xA0);
    mfrc522.PCD_WriteRegister(mfrc522.DivIEnReg, 0x80);

    // Hapus interrupt flag
    mfrc522.PCD_WriteRegister(mfrc522.ComIrqReg, 0x7F);

    // Debug MFRC522
    byte v = mfrc522.PCD_ReadRegister(mfrc522.VersionReg);
    xSemaphoreTake(serialMutex, pdMS_TO_TICKS(100));
    Serial.printf("[Input] MFRC522 version: 0x%02X\n", v);
    if (v == 0x00 || v == 0xFF) {
        Serial.println("[Input] ERROR: MFRC522 tidak terdeteksi!");
    } else {
        Serial.println("[Input] MFRC522 OK");
    }
    xSemaphoreGive(serialMutex);

    xSemaphoreTake(serialMutex, pdMS_TO_TICKS(100));
    Serial.println("[Input] Task started - waiting for RFID data...");
    xSemaphoreGive(serialMutex);

    while (1) {
        // Cek kartu baru tanpa semaphore (polling mode)
        if (mfrc522.PICC_IsNewCardPresent() && mfrc522.PICC_ReadCardSerial()) {

            // Debounce
            static uint32_t last_scan_time = 0;
            uint32_t now = millis();
            if (now - last_scan_time < 500) {
                mfrc522.PICC_HaltA();
                vTaskDelayUntil(&xLastWakeTime, pdMS_TO_TICKS(INPUT_TASK_PERIOD));
                continue;
            }
            last_scan_time = now;

            // Ambil UID (4 byte)
            rfidData.timestamp = now;
            rfidData.uid[0] = mfrc522.uid.uidByte[0];
            rfidData.uid[1] = mfrc522.uid.uidByte[1];
            rfidData.uid[2] = mfrc522.uid.uidByte[2];
            rfidData.uid[3] = mfrc522.uid.uidByte[3];

            mfrc522.PICC_HaltA();
            mfrc522.PCD_StopCrypto1();

            // Kirim ke authTask
            xStatus = xQueueSend(rfidDataQueue, &rfidData, pdMS_TO_TICKS(10));

            xSemaphoreTake(serialMutex, pdMS_TO_TICKS(100));
            if (xStatus == pdPASS) {
                Serial.printf("[Input] UID detected: %02X%02X%02X%02X at %lu ms\n",
                    rfidData.uid[0], rfidData.uid[1],
                    rfidData.uid[2], rfidData.uid[3],
                    rfidData.timestamp);
            } else {
                Serial.println("[Input] ERROR: Queue full, dropped UID");
            }
            xSemaphoreGive(serialMutex);
        }

        vTaskDelayUntil(&xLastWakeTime, pdMS_TO_TICKS(INPUT_TASK_PERIOD));
    }
}