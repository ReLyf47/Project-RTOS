#include <Arduino.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include <WiFi.h>
#include <Wire.h>

#include "config.h"
#include "data_structures.h"
#include "tasks.h"
#include "security.h"
#include "comm.h"

// ============ GLOBAL OBJECTS ============

// Queues
QueueHandle_t rfidDataQueue  = NULL;
QueueHandle_t eventLogQueue  = NULL;

// Semaphores
SemaphoreHandle_t rfidReadSemaphore     = NULL;
SemaphoreHandle_t wifiConnectedSemaphore = NULL;

// Mutexes
SemaphoreHandle_t serialMutex = NULL;

// Shared state
SecurityState securityState;

// ============ ISR ============

void IRAM_ATTR rfidISR() {
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    xSemaphoreGiveFromISR(rfidReadSemaphore, &xHigherPriorityTaskWoken);
    portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
}

// ============ INITIALIZATION ============

void initializeSynchronization() {
    rfidDataQueue  = xQueueCreate(RFID_DATA_QUEUE_SIZE, sizeof(RFIDData));
    eventLogQueue  = xQueueCreate(EVENT_LOG_QUEUE_SIZE,  sizeof(EventLog));

    rfidReadSemaphore     = xSemaphoreCreateBinary();
    wifiConnectedSemaphore = xSemaphoreCreateBinary();
    serialMutex           = xSemaphoreCreateMutex();

    if (!rfidDataQueue || !eventLogQueue ||
        !rfidReadSemaphore || !serialMutex) {
        Serial.println("[ERROR] Failed to create FreeRTOS objects!");
        while (1) vTaskDelay(pdMS_TO_TICKS(100));
    }
}

void setupHardware() {
    Serial.begin(115200);
    delay(500);
    Serial.println("\n\n=== FreeRTOS RFID Attendance System (XAMPP backend) ===\n");

    pinMode(BUZZER_PIN, OUTPUT);
    pinMode(GREEN_LED,  OUTPUT);
    pinMode(RED_LED,    OUTPUT);
    pinMode(RELAY_PIN,  OUTPUT);
    //pinMode(RFID_INT_PIN, INPUT);

    digitalWrite(BUZZER_PIN, LOW);
    digitalWrite(GREEN_LED,  LOW);
    digitalWrite(RED_LED,    LOW);
    digitalWrite(RELAY_PIN,  LOW);
    ledcAttachPin(BUZZER_PIN, 0);  // pin, channel
    ledcSetup(0, 1000, 8);         // channel, freq, resolution

    Wire.begin(21, 22);   // SDA=21, SCL=22
    Serial.println("[BOOT] Hardware initialized");
}

void setupWiFi() {
    Serial.printf("[BOOT] Connecting to WiFi: %s\n", WIFI_SSID);

    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 20) {
        delay(500);
        Serial.print(".");
        attempts++;
    }
    Serial.println();

    if (WiFi.status() == WL_CONNECTED) {
        xSemaphoreGive(wifiConnectedSemaphore);
        Serial.printf("[BOOT] WiFi connected — IP: %s\n", WiFi.localIP().toString().c_str());
        Serial.printf("[BOOT] XAMPP server: %s\n", XAMPP_BASE_URL);
    } else {
        Serial.println("[BOOT] WARNING: WiFi not connected — auth will fail until reconnected");
    }
}

void createAllTasks() {
    xTaskCreate(vInputTask,    "InputTask",    INPUT_TASK_STACK,    NULL, INPUT_PRIORITY,    NULL);
    xTaskCreate(vAuthTask,     "AuthTask",     AUTH_TASK_STACK,     NULL, AUTH_PRIORITY,     NULL);
    xTaskCreate(vCommTask,     "CommTask",     COMM_TASK_STACK,     NULL, COMM_PRIORITY,     NULL);
    xTaskCreate(vSecurityTask, "SecurityTask", SECURITY_TASK_STACK, NULL, SECURITY_PRIORITY, NULL);
    xTaskCreate(vDisplayTask,  "DisplayTask",  DISPLAY_TASK_STACK,  NULL, DISPLAY_PRIORITY,  NULL);

    Serial.println("[BOOT] All 5 FreeRTOS tasks created");
}

// ============ MAIN ============

void setup() {
    setupHardware();
    initializeSynchronization();

    // Zero out security state
    memset(&securityState, 0, sizeof(SecurityState));

    setupWiFi();

    attachInterrupt(digitalPinToInterrupt(RFID_INT_PIN), rfidISR, FALLING);
    Serial.printf("[BOOT] ISR attached to GPIO %d\n", RFID_INT_PIN);

    createAllTasks();
}

void loop() {
    vTaskDelay(pdMS_TO_TICKS(1000));

    static int counter = 0;
    if (++counter >= 10) {
        counter = 0;
        printTaskStats();
    }
}

// ============ UTILITY ============

void printTaskStats() {
    xSemaphoreTake(serialMutex, pdMS_TO_TICKS(100));
    Serial.println("\n=== SYSTEM STATISTICS ===");
    Serial.printf("WiFi     : %s  (%d dBm)\n",
                  isWiFiConnected() ? "Connected" : "Disconnected",
                  getWiFiSignalStrength());
    Serial.printf("Locked   : %s\n", isSystemLocked(&securityState) ? "YES" : "no");
    Serial.printf("Attempts : %d / %d\n",
                  securityState.failed_attempts, MAX_FAILED_ATTEMPTS);
    Serial.println("=========================\n");
    xSemaphoreGive(serialMutex);
}