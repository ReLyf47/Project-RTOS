#include <Arduino.h>
#include "security.h"
#include "config.h"
#include <stdio.h>
#include <string.h>

// ============ ROLLING TOKEN ALGORITHM IMPLEMENTATION ============

/**
 * @brief Calculate new UID based on rolling token algorithm
 * 
 * Algorithm:
 *  - UID format: 4 bytes [AA BB CC DD]
 *  - Extract last 3 hex digits (12 bits): DD >> 4 | (CC & 0x0F) << 4
 *  - Add ROLLING_TOKEN_STEP (1)
 *  - Apply modulo 0xFFF
 *  - Combine with prefix (AA BB)
 */
int calculateRollingToken(uint8_t *current_uid, uint8_t *new_uid) {
    if (current_uid == NULL || new_uid == NULL) {
        return -1;
    }

    // Copy prefix (first 3 bytes)
    new_uid[0] = current_uid[0];
    new_uid[1] = current_uid[1];
    new_uid[2] = current_uid[2];

    // Extract last 3 hex digits (12 bits)
    // From bytes: [B0 B1 B2 B3]
    // Last 12 bits = (B3 << 4) | (B2 & 0x0F)
    uint16_t current_suffix = ((uint16_t)current_uid[3] << 4) | (current_uid[2] & 0x0F);
    
    // Calculate new suffix: (current + STEP) mod 0xFFF
    uint16_t new_suffix = (current_suffix + ROLLING_TOKEN_STEP) & UID_MASK_LAST_3;
    
    // Decompose new suffix back to bytes
    new_uid[2] = (new_uid[2] & 0xF0) | ((new_suffix >> 4) & 0x0F);
    new_uid[3] = (uint8_t)(new_suffix & 0xFF);

    return 0;
}

/**
 * @brief Verify UID against database and get user info
 */
int verifyUID(uint8_t *scanned_uid, UIDDatabase *db, UIDEntry *out_entry) {
    if (scanned_uid == NULL || db == NULL) {
        return -1;
    }

    for (int i = 0; i < db->count; i++) {
        // Convert hex string to binary
        uint8_t db_uid_binary[4];
        if (uidHexToBinary(db->entries[i].uid, db_uid_binary) != 0) {
            continue;
        }

        // Compare UIDs
        if (uidCompare(scanned_uid, db_uid_binary)) {
            if (out_entry != NULL) {
                memcpy(out_entry, &db->entries[i], sizeof(UIDEntry));
            }
            return i;  // Return index of matching entry
        }
    }

    return -1;  // UID not found
}

/**
 * @brief Update UID in database with new rolling token value
 */
int updateUIDInDatabase(UIDDatabase *db, int uid_index, uint8_t *new_uid) {
    if (db == NULL || new_uid == NULL || uid_index < 0 || uid_index >= db->count) {
        return -1;
    }

    // Convert new UID to hex string
    if (uidBinaryToHex(new_uid, db->entries[uid_index].uid) != 0) {
        return -1;
    }

    // Update timestamp
    db->entries[uid_index].timestamp_reg = millis();
    db->last_sync = millis();

    return 0;
}

/**
 * @brief Convert 4-byte binary UID to hex string
 */
int uidBinaryToHex(uint8_t *binary_uid, char *hex_string) {
    if (binary_uid == NULL || hex_string == NULL) {
        return -1;
    }

    sprintf(hex_string, "%02X%02X%02X%02X",
            binary_uid[0], binary_uid[1],
            binary_uid[2], binary_uid[3]);

    return 0;
}

/**
 * @brief Convert hex string UID to 4-byte binary
 */
int uidHexToBinary(const char *hex_string, uint8_t *binary_uid) {
    if (hex_string == NULL || binary_uid == NULL) {
        return -1;
    }

    // Verify length is 8 characters
    if (strlen(hex_string) != 8) {
        return -1;
    }

    // Parse hex string
    int result = sscanf(hex_string, "%2hhX%2hhX%2hhX%2hhX",
                        &binary_uid[0], &binary_uid[1],
                        &binary_uid[2], &binary_uid[3]);

    if (result != 4) {
        return -1;
    }

    return 0;
}

/**
 * @brief Compare two 4-byte UIDs
 */
int uidCompare(uint8_t *uid1, uint8_t *uid2) {
    if (uid1 == NULL || uid2 == NULL) {
        return 0;
    }

    return (uid1[0] == uid2[0] &&
            uid1[1] == uid2[1] &&
            uid1[2] == uid2[2] &&
            uid1[3] == uid2[3]) ? 1 : 0;
}

/**
 * @brief Copy UID from source to destination
 */
void uidCopy(uint8_t *src, uint8_t *dst) {
    if (src == NULL || dst == NULL) {
        return;
    }

    dst[0] = src[0];
    dst[1] = src[1];
    dst[2] = src[2];
    dst[3] = src[3];
}

// ============ SECURITY STATE MANAGEMENT ============

/**
 * @brief Record a failed access attempt
 */
int recordFailedAttempt(SecurityState *state) {
    if (state == NULL) {
        return -1;
    }

    state->failed_attempts++;
    state->last_attempt = millis();

    // Check if we exceed max attempts
    if (state->failed_attempts >= MAX_FAILED_ATTEMPTS) {
        lockSystem(state, LOCKOUT_DURATION_MS);
        return state->failed_attempts;
    }

    return state->failed_attempts;
}

/**
 * @brief Check if system is currently locked
 */
int isSystemLocked(SecurityState *state) {
    if (state == NULL) {
        return 0;
    }

    if (!state->is_locked) {
        return 0;
    }

    // Check if lockout has expired
    uint32_t current_time = millis();
    if (current_time >= state->lockout_until) {
        state->is_locked = 0;
        state->failed_attempts = 0;
        return 0;
    }

    return 1;
}

/**
 * @brief Clear failed attempts and unlock system
 */
void clearFailedAttempts(SecurityState *state) {
    if (state == NULL) {
        return;
    }

    state->failed_attempts = 0;
    state->is_locked = 0;
    state->lockout_until = 0;
}

/**
 * @brief Lock system for specified duration
 */
void lockSystem(SecurityState *state, uint32_t duration_ms) {
    if (state == NULL) {
        return;
    }

    state->is_locked = 1;
    state->lockout_until = millis() + duration_ms;
}

// ============ HARDWARE FEEDBACK ============

/**
 * @brief Give success feedback (green LED + beep)
 */
void feedbackSuccess() {
    digitalWrite(GREEN_LED, HIGH);
    vTaskDelay(pdMS_TO_TICKS(LED_SUCCESS_PULSE));
    digitalWrite(GREEN_LED, LOW);

    tone(BUZZER_PIN, 1000);
    vTaskDelay(pdMS_TO_TICKS(BUZZER_SUCCESS_MS));
    noTone(BUZZER_PIN);
}

void feedbackFailure() {
    digitalWrite(RED_LED, HIGH);
    vTaskDelay(pdMS_TO_TICKS(LED_FAIL_PULSE));
    digitalWrite(RED_LED, LOW);

    tone(BUZZER_PIN, 400);
    vTaskDelay(pdMS_TO_TICKS(150));
    noTone(BUZZER_PIN);
    vTaskDelay(pdMS_TO_TICKS(100));
    tone(BUZZER_PIN, 400);
    vTaskDelay(pdMS_TO_TICKS(150));
    noTone(BUZZER_PIN);
}

void feedbackLockout() {
    for (int i = 0; i < 3; i++) {
        digitalWrite(RED_LED, HIGH);
        vTaskDelay(pdMS_TO_TICKS(100));
        digitalWrite(RED_LED, LOW);
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    tone(BUZZER_PIN, 300);
    vTaskDelay(pdMS_TO_TICKS(500));
    noTone(BUZZER_PIN);
}