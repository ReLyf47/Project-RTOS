#ifndef SPIFFS_MANAGER_H
#define SPIFFS_MANAGER_H

#include <stdint.h>

// ============ UID DATABASE STRUCTURES ============

#define MAX_UID_ENTRIES     20
#define UID_HEX_LEN         9    // "12345678\0"
#define UID_NAME_LEN        50
#define UID_VERSION_LEN     16

typedef struct {
    char     uid[UID_HEX_LEN];       // 8-char hex string + null terminator
    char     name[UID_NAME_LEN];     // Human-readable name
    uint32_t timestamp_reg;          // millis() when registered
    uint32_t rolling_state;          // Current rolling token state
} UIDEntry;

typedef struct {
    UIDEntry entries[MAX_UID_ENTRIES];
    int      count;
    uint32_t last_sync;
    char     version[UID_VERSION_LEN];
} UIDDatabase;

// ============ SPIFFS FUNCTION DECLARATIONS ============

/**
 * @brief Initialize SPIFFS filesystem
 * @return 0 on success, -1 on failure
 */
int initSPIFFS();

/**
 * @brief Check if SPIFFS is mounted
 * @return 1 if mounted, 0 otherwise
 */
int isSPIFFSMounted();

/**
 * @brief Load UID database from JSON file
 * @param db Pointer to UIDDatabase to populate
 * @param filepath Path in SPIFFS (e.g. "/uids.json")
 * @return 0 on success, -1 on error
 */
int loadUIDsFromFile(UIDDatabase *db, const char *filepath);

/**
 * @brief Save UID database to JSON file
 * @param db Pointer to UIDDatabase to save
 * @param filepath Path in SPIFFS
 * @return 0 on success, -1 on error
 */
int saveUIDsToFile(UIDDatabase *db, const char *filepath);

/**
 * @brief Add a single UID entry to the file
 * @return 0 on success, -1 on error or duplicate
 */
int addUIDToFile(UIDEntry *entry, const char *filepath);

/**
 * @brief Delete a UID entry from the file by hex string
 * @return 0 on success, -1 if not found
 */
int deleteUIDFromFile(const char *uid_hex, const char *filepath);

/**
 * @brief Check if a UID exists in the file
 * @return 1 if exists, 0 otherwise
 */
int uidExistsInFile(const char *uid_hex, const char *filepath);

/**
 * @brief Check if a file exists in SPIFFS
 * @return 1 if exists, 0 otherwise
 */
int fileExists(const char *filepath);

/**
 * @brief Delete a file from SPIFFS
 * @return 0 on success, -1 on error
 */
int deleteFile(const char *filepath);

/**
 * @brief Get size of a file in bytes
 * @return File size, or -1 on error
 */
int getFileSize(const char *filepath);

/**
 * @brief Print all files in SPIFFS to Serial
 */
void listSPIFFSFiles();

/**
 * @brief Get SPIFFS storage info
 * @param total_bytes Output: total storage bytes
 * @param used_bytes  Output: used storage bytes
 * @return 0 on success, -1 on error
 */
int getSPIFFSInfo(uint32_t *total_bytes, uint32_t *used_bytes);

#endif // SPIFFS_MANAGER_H
