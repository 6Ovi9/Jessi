#ifndef CONFIG_H
#define CONFIG_H

#include <stdint.h>

// ============================================================================
// HARDWARE PINS (XIAO nRF52840 Sense)
// ============================================================================

#define PIN_LED_RING_DATA    D7    // SK6812 data (NeoPixel protocol)
#define PIN_LED_POWER        D10   // MOSFET gate (HIGH = LEDs powered, LOW = sleep)
#define PIN_BUTTON_TOUCH     D8    // TTP223 capacitive button
#define PIN_MOTOR            D9    // MOSFET gate (HIGH = vibrate, LOW = off)
#define PIN_COMPASS_SDA      D4    // LIS3MDL I2C (custom pins)
#define PIN_COMPASS_SCL      D5    // LIS3MDL I2C
#define PIN_IMU_INT1         PIN_LSM6DS3TR_C_INT1  // LSM6DS3 wake-on-motion interrupt (typically P0.11)

// ============================================================================
// LED RING CONFIG
// ============================================================================

#define LED_COUNT            12    // 12× SK6812 MINI-E (RGB+W)
#define LED_BRIGHTNESS_MAX   255   // Max PWM value
#define LED_CLOCK_BRIGHTNESS 30    // Default brightness % for CLOCK mode (will be applied as 0-255)

// LED positions (physical mapping)
// LED 0 = 12h (top), LED 3 = 3h (right), LED 6 = 6h (bottom), LED 9 = 9h (left)
// Clockwise order

#define DUMMY_BRIGHTNESS_LOGARITHMIC 2.2f // Gamma correction exponent

// ============================================================================
// BUTTON & GESTURE TIMING (TTP223 debounce + gesture detection)
// ============================================================================

#define DEBOUNCE_MS          20    // Software debounce
#define TAP_MIN_MS           50    // Minimum tap duration
#define TAP_MAX_MS           500   // Maximum tap duration (>500ms = press)
#define DOUBLE_TAP_WINDOW_MS 400   // Gap between first tap end and second tap start
#define PRESS_SHORT_MS       1500  // Press short: 1.5s to 3s
#define PRESS_LONG_MS        3000  // Press long: ≥3s

// ============================================================================
// STATE TIMERS (auto-transition timing)
// ============================================================================

#define TIMER_CLOCK_TIMEOUT_MS        10000   // CLOCK → DEEP_SLEEP timeout (default)
#define TIMER_RADAR_TIMEOUT_MS        5000    // RADAR/DISTANCE → CLOCK timeout
#define TIMER_HAPTIC_RX_TIMEOUT_MS    3000    // HAPTIC_RX → previous state (safety)
#define TIMER_WAKING_UP_MS            200     // WAKING_UP transitional time
#define TIMER_LOW_BATTERY_PULSE_MS    30000   // 30s between battery warning pulses
#define TIMER_ERROR_NO_GPS_MS         2000    // ERROR_NO_GPS display duration

// ============================================================================
// POWER MANAGEMENT & BATTERY
// ============================================================================

#define DEBUG_BYPASS_BATTERY_CHECK     0      // 1 = bypass battery checks for USB testing, 0 = normal operation
#define DEBUG_DISABLE_DEEP_SLEEP       0      // 1 = stay awake for testing, 0 = normal sleep behavior
#define DEBUG_VERBOSE_OUTPUT           1      // 1 = detailed debug logs, 0 = minimal output

#define LOW_BATTERY_THRESHOLD_PERCENT  15     // % threshold to show LOW_BATTERY overlay
#define CRITICAL_BATTERY_PERCENT       5      // % threshold to force DEEP_SLEEP

#define DEEP_SLEEP_CURRENT_TARGET_UA   10     // Target <10µA in DEEP_SLEEP

// ============================================================================
// BLE CONFIGURATION
// ============================================================================

#define BLE_SERVICE_NAME             "Nexus Halo"
#define BLE_DEVICE_NAME              "Nexus Halo"
#define BLE_ADVERTISING_INTERVAL_MS  100     // Minimal advertising in DEEP_SLEEP

// Custom BLE service UUID (128-bit)
// Format: 12345678-1234-5678-1234-56789ABCDEF0 (example)
#define BLE_SERVICE_UUID_HI          0x12345678UL
#define BLE_SERVICE_UUID_LO          0x56789ABCUL

// BLE Characteristic UUIDs (128-bit custom, or standard for standard chars)
// For now, we'll use simple enum offsets; library will handle actual UUIDs
enum BLE_CHAR_ID {
  BLE_CHAR_BEARING = 0,        // WRITE (0-360 degrees, float)
  BLE_CHAR_DISTANCE = 1,       // WRITE (meters, uint32)
  BLE_CHAR_HAPTIC_TX = 2,      // NOTIFY (reloj → móvil: user tapped)
  BLE_CHAR_HAPTIC_RX = 3,      // WRITE (móvil → reloj: vibrate)
  BLE_CHAR_RADAR_ACTIVE = 4,   // NOTIFY (0x01 = on, 0x00 = off)
  BLE_CHAR_CONFIG = 5,         // WRITE+READ (JSON config)
  BLE_CHAR_BATTERY = 6,        // NOTIFY (%)
  BLE_CHAR_OTA = 7,            // Nordic DFU
  BLE_CHAR_CALIB_CMD = 8,      // WRITE (0x01=start, 0x02=end, 0x03=cancel)
  BLE_CHAR_CALIB_STATUS = 9,   // NOTIFY (num_samples_done / total_samples)
  BLE_CHAR_CALIB_THRESHOLD = 10  // WRITE+READ (current wake threshold 0x00-0xFF)
};

// ============================================================================
// COMPASS (LIS3MDL) CALIBRATION
// ============================================================================

#define COMPASS_I2C_ADDRESS  0x1C   // Default I2C address for LIS3MDL
#define COMPASS_UPDATE_RATE_HZ 10   // Update frequency (10Hz para no saturar I2C)
#define COMPASS_HEADING_ALPHA  0.5f // Alpha alto (0.5) para que siga reaccionando rápido al giro

// Hard-iron & soft-iron offsets (calibration)
// Defaults; can be overwritten by app config
#define COMPASS_HARD_IRON_X   0.0f
#define COMPASS_HARD_IRON_Y   0.0f
#define COMPASS_HARD_IRON_Z   0.0f
#define COMPASS_SOFT_IRON_XX  1.0f
#define COMPASS_SOFT_IRON_YY  1.0f
#define COMPASS_SOFT_IRON_ZZ  1.0f

// ============================================================================
// VIBRATION PATTERNS (Motor PWM durations in ms)
// ============================================================================

// Pattern structure: { on_ms, off_ms } array, terminate with {0, 0}
// See haptic.h for pattern definitions

#define HAPTIC_PATTERN_RX_DURATION_MS  (200+100+200+100+400)  // Total time
#define HAPTIC_PATTERN_TX_DURATION_MS  (100+100+100+100)      // 2 flashes

// ============================================================================
// COLOR SCHEMES (ARGB8888 format: 0xAARRGGBB)
// ============================================================================

// CLOCK_CONNECTED (pink)
#define COLOR_HOURS_CONNECTED    0xFFFF6699   // Pink
#define COLOR_MINUTES_CONNECTED  0xFFFF6699   // Pink
#define COLOR_SECONDS_CONNECTED  0xFFFF6699   // Pink

// CLOCK_DISCONNECTED (pink)
#define COLOR_HOURS_DISC         0xFFFF6699   // Pink
#define COLOR_MINUTES_DISC       0xFFFF6699   // Pink
#define COLOR_SECONDS_DISC       0xFFFF6699   // Pink

// RADAR & DISTANCE modes
#define COLOR_RADAR              0xFFFFB900   // Warm amber
#define COLOR_DISTANCE_NEAR      0xFF0080FF   // Blue (0-15km)
#define COLOR_DISTANCE_PROVINCE  0xFF00CC44   // Green (15-50km)
#define COLOR_DISTANCE_FAR       0xFFFFCC00   // Yellow (50-150km)
#define COLOR_DISTANCE_VFAR      0xFFFF6600   // Orange (150-350km)
#define COLOR_DISTANCE_EXTREME   0xFFFF0000   // Red (350-500km)

// Status indicators
#define COLOR_ERROR              0xFFFF0000   // Red (errors)
#define COLOR_SUCCESS            0xFF00FF00   // Green (OTA success)
#define COLOR_WARNING            0xFFFFCC00   // Yellow (warnings)
#define COLOR_INFO               0xFF00CCFF   // Cyan (info)
#define COLOR_HAPTIC_RX          0xFFFF6699   // Pink (haptic receive)

// ============================================================================
// DISTANCE THRESHOLDS (for DISTANCE_MODE LED filling)
// ============================================================================

#define DISTANCE_THRESHOLD_1_KM    15      // Border between NEAR and PROVINCE
#define DISTANCE_THRESHOLD_2_KM    50      // Border between PROVINCE and FAR
#define DISTANCE_THRESHOLD_3_KM    150     // Border between FAR and VFAR
#define DISTANCE_THRESHOLD_4_KM    350     // Border between VFAR and EXTREME
#define DISTANCE_THRESHOLD_MAX_KM  500     // Maximum displayable distance

// ============================================================================
// GPS DYNAMIC POLLING MODES (handled in app, but referenced by firmware)
// ============================================================================

#define GPS_POLLING_PRECISION_S    3       // <500m or RADAR_MODE active
#define GPS_POLLING_NEAR_S         60      // <10km
#define GPS_POLLING_FAR_S          180     // 10-50km
#define GPS_POLLING_REMOTE_MIN_S   300     // >50km (min 5min)
#define GPS_POLLING_REMOTE_MAX_S   600     // >50km (max 10min, with jitter)

// ============================================================================
// OTA (Over-The-Air Update)
// ============================================================================

#define OTA_TIMEOUT_MS            120000   // 2 min timeout for OTA
#define OTA_LED_UPDATE_INTERVAL_MS 500     // LED progress update rate

// ============================================================================
// RISE-TO-WAKE (LSM6DS3 Wake-on-Motion)
// ============================================================================

#define IMU_WAKE_ENABLED           1       // 1 = enabled, 0 = disabled (use only tap)
#define IMU_ACCEL_RATE_HZ          26      // Low-power rate (26Hz minimum)
#define IMU_ACCEL_RANGE_G          2       // ±2G range (sufficient for wrist motion)

// Wake-up threshold: 1 LSB = 62.5 mg at ±2G, 6-bit field (bits[5:0])
//   0x02 = 125 mg  ← too sensitive, triggers on table vibrations
//   0x08 = 500 mg  ← good for wrist raise (recommended)
//   0x10 = 1000 mg ← only strong flicks
//   0x20 = 2000 mg ← maximum practical
#define IMU_WAKE_UP_THS_DEFAULT    0x08    // 500 mg — firm wrist raise needed
#define IMU_WAKE_UP_THS_MIN        0x01    // 62.5 mg (most sensitive)
#define IMU_WAKE_UP_THS_MAX        0x3F    // 3.94 G (least sensitive, max 6-bit)

// Gyroscope wrist-flick gesture sensitivity (dps)
#define GESTURE_GYRO_THS_DEFAULT   260     // 260 dps — default wrist flick threshold
#define GESTURE_GYRO_THS_MIN       100     // 100 dps
#define GESTURE_GYRO_THS_MAX       500     // 500 dps

// Gyroscope double-flick timing window (ms)
#define GESTURE_DOUBLE_FLICK_WINDOW_DEFAULT 800  // 800 ms
#define GESTURE_DOUBLE_FLICK_WINDOW_MIN     400  // 400 ms
#define GESTURE_DOUBLE_FLICK_WINDOW_MAX     1200 // 1200 ms

// Duration: consecutive samples that must exceed threshold before INT1 fires
// At 26 Hz each sample = 38 ms
//   0x00 = 1 sample  = 38 ms  ← triggers on single-sample spikes/noise
//   0x01 = 2 samples = 77 ms
//   0x02 = 3 samples = 115 ms ← filters out most vibrations (recommended)
#define IMU_WAKE_UP_DUR            0x01    // 2 samples = ~77ms sustained motion

// Calibration constants
#define CALIBRATION_NUM_SAMPLES    5       // Number of gestures to capture
#define CALIBRATION_TIMEOUT_MS     30000   // 30 seconds to complete calibration
#define CALIBRATION_MIN_ACCEL_MG   200     // Minimum acceleration to register as valid gesture (mg)
#define CALIBRATION_BUFFER_SIZE    256     // Max accel samples per gesture

// EEPROM/Flash storage
#define EEPROM_CALIB_ADDR          0x0000  // Start address for calibration data in flash
#define EEPROM_CALIB_SIZE          64      // Bytes reserved for calibration (wake threshold + metadata)
#define EEPROM_CALIB_MAGIC         0xCAFEU // Magic number to detect valid calibration data

// ============================================================================
// DATA TYPES & ENUMS
// ============================================================================

enum State {
  STATE_DEEP_SLEEP = 0,
  STATE_WAKING_UP,
  STATE_CLOCK_CONNECTED,
  STATE_CLOCK_DISCONNECTED,
  STATE_RADAR_MODE,
  STATE_DISTANCE_MODE,
  STATE_HAPTIC_TX,
  STATE_HAPTIC_RX,
  STATE_OTA_MODE,
  STATE_ERROR_NO_GPS,
  STATE_CALIBRATION_MODE,     // NEW: Rise-to-wake calibration
  STATE_LOW_BATTERY           // Superimposed, not primary
};

enum GestureType {
  GESTURE_NONE = 0,
  GESTURE_TAP_SIMPLE,
  GESTURE_TAP_DOUBLE,
  GESTURE_PRESS_SHORT,
  GESTURE_PRESS_LONG
};

enum BLEEvent {
  BLE_EVENT_NONE = 0,
  BLE_EVENT_CONNECTED,
  BLE_EVENT_DISCONNECTED,
  BLE_EVENT_BEARING_UPDATED,
  BLE_EVENT_DISTANCE_UPDATED,
  BLE_EVENT_HAPTIC_RX,
  BLE_EVENT_CONFIG_UPDATED,
  BLE_EVENT_RADAR_MODE_REQUEST,
  BLE_EVENT_OTA_START,
  BLE_EVENT_OTA_END,
  BLE_EVENT_CALIB_START,        // NEW: Start rise-to-wake calibration
  BLE_EVENT_CALIB_END,          // NEW: End calibration
  BLE_EVENT_CALIB_CANCEL        // NEW: Cancel calibration
};

enum WakeSource {
  WAKE_SOURCE_NONE = 0,
  WAKE_SOURCE_TAP,              // D8 button tap
  WAKE_SOURCE_MOTION            // IMU wake-on-motion
};

// ============================================================================
// STRUCTS
// ============================================================================

typedef struct {
  float bearing;        // 0-360 degrees (relative to compass heading)
  uint32_t distance_m;  // Distance in meters
  bool radar_mode_active;
  bool gps_has_fix;
} BLE_LocationData;

typedef struct {
  uint8_t percentage;   // 0-100%
} BLE_BatteryData;

typedef struct {
  uint32_t compass_offset_x;
  uint32_t compass_offset_y;
  uint32_t compass_offset_z;
  uint8_t brightness_percent;
  uint8_t low_battery_threshold;
  uint32_t clock_color_hours;
  uint32_t clock_color_minutes;
  uint32_t clock_color_seconds;
  // ... more fields
} WatchConfig;

#endif // CONFIG_H
