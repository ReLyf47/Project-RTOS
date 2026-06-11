#ifndef CONFIG_H
#define CONFIG_H

#include <stdint.h>

// ============ WIFI CONFIGURATION ============
#define WIFI_SSID           "Jane Wifi"         // TODO: Update
#define WIFI_PASSWORD       "jane116frozen"      // TODO: Update
#define WIFI_TIMEOUT_MS     10000

// ============ XAMPP SERVER CONFIGURATION ============
// Set this to your PC's local IP address (check via ipconfig / ifconfig)
#define XAMPP_SERVER_IP     "192.168.101.74"   // TODO: Update to your PC's IP
#define XAMPP_SERVER_PORT   80
#define XAMPP_BASE_URL      "http://192.168.101.74/rfid"

// ============ GPIO PIN DEFINITIONS ============
#define BUZZER_PIN          14
#define GREEN_LED           25
#define RED_LED             26
#define RELAY_PIN           27
#define RFID_INT_PIN        13

// ============ I2C LCD CONFIGURATION ============
#define LCD_ADDR            0x27
#define LCD_COLS            16
#define LCD_ROWS            2

// ============ FREERTOS TASK PERIODS (ms) ============
#define INPUT_TASK_PERIOD   50
#define AUTH_TASK_PERIOD    100
#define COMM_TASK_PERIOD    5000
#define SECURITY_TASK_PERIOD 1000
#define DISPLAY_TASK_PERIOD 250

// ============ FREERTOS TASK STACK SIZES (bytes) ============
#define INPUT_TASK_STACK    2048
#define AUTH_TASK_STACK     6144    // Larger: handles HTTP client calls
#define COMM_TASK_STACK     4096
#define SECURITY_TASK_STACK 2048
#define DISPLAY_TASK_STACK  2048

// ============ FREERTOS TASK PRIORITIES ============
#define INPUT_PRIORITY      3
#define AUTH_PRIORITY       3
#define COMM_PRIORITY       2
#define SECURITY_PRIORITY   2
#define DISPLAY_PRIORITY    1

// ============ QUEUE SIZES ============
#define RFID_DATA_QUEUE_SIZE    5
#define EVENT_LOG_QUEUE_SIZE    10

// ============ ROLLING TOKEN CONFIGURATION ============
#define ROLLING_TOKEN_STEP  1
#define UID_MASK_LAST_3     0xFFF

// ============ SECURITY CONFIGURATION ============
#define MAX_FAILED_ATTEMPTS 3
#define LOCKOUT_DURATION_MS 30000

// API Endpoints (PHP scripts in htdocs/rfid/api/)
#define API_VERIFY_UID      XAMPP_BASE_URL "/api/verify.php"
#define API_UPDATE_UID      XAMPP_BASE_URL "/api/update_uid.php"
#define API_LOG_EVENT       XAMPP_BASE_URL "/api/log_event.php"
#define API_HEARTBEAT       XAMPP_BASE_URL "/api/heartbeat.php"

#define HTTP_TIMEOUT_MS     5000    // Max wait for server response

// ============ LED FEEDBACK TIMINGS (ms) ============
#define LED_SUCCESS_PULSE   50
#define LED_FAIL_PULSE      200
#define BUZZER_SUCCESS_MS   100
#define BUZZER_FAIL_MS      300

// ============ DEBUG LOGGING ============
#define DEBUG_SERIAL        1

#endif // CONFIG_H