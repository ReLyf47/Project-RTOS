#ifndef SECURITY_H
#define SECURITY_H

#include <stdint.h>
#include "data_structures.h"
#include "spiffs_manager.h"   // provides UIDDatabase and UIDEntry

// ============ ROLLING TOKEN ALGORITHM ============

int calculateRollingToken(uint8_t *current_uid, uint8_t *new_uid);

int verifyUID(uint8_t *scanned_uid, UIDDatabase *db, UIDEntry *out_entry);

int updateUIDInDatabase(UIDDatabase *db, int uid_index, uint8_t *new_uid);

int uidBinaryToHex(uint8_t *binary_uid, char *hex_string);

int uidHexToBinary(const char *hex_string, uint8_t *binary_uid);

int uidCompare(uint8_t *uid1, uint8_t *uid2);

void uidCopy(uint8_t *src, uint8_t *dst);

// ============ SECURITY STATE MANAGEMENT ============

int recordFailedAttempt(SecurityState *state);

int isSystemLocked(SecurityState *state);

void clearFailedAttempts(SecurityState *state);

void lockSystem(SecurityState *state, uint32_t duration_ms);

// ============ HARDWARE FEEDBACK ============

void feedbackSuccess();

void feedbackFailure();

void feedbackLockout();

#endif // SECURITY_H