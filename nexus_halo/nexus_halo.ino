/*
  ============================================================================
  NEXUS HALO — Firmware for Seeed Studio XIAO nRF52840 Sense
  ============================================================================

  Smart ring watch with LED display, magnetometer compass, gesture detection,
  and BLE communication with mobile app. Designed for proximity-based couple
  tracking with haptic feedback and dynamic distance/bearing indicators.

  Hardware:
  - Seeed XIAO nRF52840 Sense (nRF52840 + BLE 5.0 + LSM6DS3 IMU)
  - 12× SK6812-MINI-E RGB+W LEDs (NeoPixel compatible)
  - LIS3MDL Magnetometer on custom I2C (D4, D5)
  - TTP223 capacitive button (D8)
  - Vibration motor via MOSFET (D9)
  - LED power MOSFET (D10)

  Version: 1.2
  Author: [Your Name]
  License: MIT
  ============================================================================
*/

// ============================================================================
// INCLUDES
// ============================================================================

#include "ble_handler.h"
#include "compass.h"
#include "config.h"
#include "eeprom_manager.h"
#include "gesture.h"
#include "haptic.h"
#include "imu_calibrator.h"
#include "led_controller.h"
#include "power.h"
#include "runtime_config.h"
#include "state_machine.h"
#include <nrf_power.h>

// Arduino/nRF52840 libraries
#include <Arduino.h>
#include <LSM6DS3.h> // Seeed official library (not Adafruit)
#include <Wire.h>

#undef Wire // Prevent LSM6DS3.h from redefining Wire to Wire1 in our code
#include <PDM.h>

// ============================================================================
// GLOBAL OBJECTS
// ============================================================================

StateMachine state_machine;
GestureDetector gesture_detector;
LEDController led_controller;
CompassController compass;
HapticController haptic;
BLEHandler ble_handler;
PowerManager power_manager;
IMUCalibrator imu_calibrator;
EEPROMManager eeprom_manager;
RuntimeConfigManager runtime_config;
LSM6DS3 lsm6ds3(I2C_MODE, 0x6A); // Seeed LSM6DS3 on I2C, address 0x6A

// Timing
uint32_t loop_last_ms = 0;
const uint32_t LOOP_INTERVAL_MS = 10; // 100 Hz main loop

// State variables
bool ble_connected = false;
float current_bearing = 0;     // From BLE
uint32_t current_distance = 0; // From BLE
uint8_t battery_percent = 100;
int last_wake_source = WAKE_SOURCE_NONE;
bool lsm6ds3_connected = false; // IMU connection status
volatile bool motion_detected_flag = false;
volatile bool config_update_pending = false;
uint32_t last_config_change_ms = 0;
bool config_save_pending = false;

// ── Time sync (Unix timestamp received from app via BLE) ────────────────────
bool time_synced = false;    // True after first sync from app
uint32_t unix_base_ts = 0;   // Unix epoch at the moment of last sync
uint32_t millis_at_sync = 0; // millis() value at the moment of last sync

/// Return current Unix timestamp (seconds since epoch).
/// Falls back to uptime if no sync has been received yet.
inline uint32_t getRealEpoch() {
  if (time_synced) {
    return unix_base_ts + (millis() - millis_at_sync) / 1000;
  }
  return millis() / 1000; // Fallback: seconds since boot
}

// ============================================================================
// SETUP
// ============================================================================

void setup() {
  // Initialize serial for debugging
  Serial.begin(115200);
  // Esperar a que se abra el puerto serie, con timeout de 5 segundos
  uint32_t t = millis();
  while (!Serial && (millis() - t < 5000)) {
    delay(10);
  }
  delay(500);

  Serial.println("[SETUP] Nexus Halo Firmware v1.2");

  // ========================================================================
  // CRITICAL: Initialize EEPROM/Flash first so LittleFS is ready
  // ========================================================================
  Serial.println("[SETUP] Initializing EEPROM...");
  eeprom_manager.begin();

  Serial.println("[SETUP] Initializing runtime config...");
  runtime_config.begin();

  // ========================================================================
  // DIAGNOSTIC I2C SCANNER IN MAIN SETUP (Mimics compass_diagnostic.ino)
  // ========================================================================
  Serial.println("\n--- RUNNING IN-SETUP I2C SCANNER (10kHz) ---");
  Wire.setPins(PIN_COMPASS_SDA, PIN_COMPASS_SCL);
  Wire.begin();
  pinMode(PIN_COMPASS_SDA, INPUT_PULLUP);
  pinMode(PIN_COMPASS_SCL, INPUT_PULLUP);
  // CRITICAL: Lowered to 10kHz because internal pullups (13k) are too weak for
  // 100kHz!
  Wire.setClock(10000);
  delay(100);

  int found_count = 0;
  for (uint8_t addr = 1; addr < 128; addr++) {
    Wire.beginTransmission(addr);
    int err = Wire.endTransmission();
    if (err == 0) {
      Serial.print("  [SCAN] Found device at 0x");
      Serial.println(addr, HEX);
      found_count++;
    }
  }
  if (found_count == 0) {
    Serial.println("  [SCAN] No devices found on Wire (D4/D5).");
  }
  Serial.println("------------------------------------\n");

  Serial.println("[SETUP] Initializing compass (LIS3MDL)...");
  compass.begin();

  // Safety pull-downs for MOSFETs to prevent floating gate issues
  pinMode(PIN_MOTOR, OUTPUT);
  digitalWrite(PIN_MOTOR, LOW);
  pinMode(PIN_LED_POWER, OUTPUT);
  digitalWrite(PIN_LED_POWER, LOW);

  // ========================================================================
  // Initialize internal I2C bus (Wire1 → LSM6DS3 IMU)
  // ========================================================================

  Serial.println("[SETUP] Powering on internal IMU...");
  pinMode(PIN_LSM6DS3TR_C_POWER, OUTPUT);
  digitalWrite(PIN_LSM6DS3TR_C_POWER, HIGH);
  delay(50);

  Serial.println("[SETUP] Initializing I2C buses (Wire & Wire1)...");
  Wire1.begin(); // Internal bus for LSM6DS3
  delay(20);

  // ========================================================================
  // Initialize LSM6DS3 (integrated accelerometer/gyroscope)
  // ========================================================================

  Serial.println("[SETUP] Initializing LSM6DS3 IMU...");
  uint32_t lsm_try = 0;
  while (!lsm6ds3_connected && lsm_try < 3) {
    if (lsm6ds3.begin() == 0) { // Seeed library: 0 = success
      lsm6ds3_connected = true;
      Serial.println("[SETUP] ✓ LSM6DS3 found and initialized (Seeed)");
      Serial.println("[SETUP] LSM6DS3 ready: 26Hz, ±2G/±250dps");
    } else {
      lsm_try++;
      Serial.print("[SETUP] LSM6DS3 init attempt ");
      Serial.print(lsm_try);
      Serial.println(" failed, retrying...");
      delay(50);
    }
  }

  if (!lsm6ds3_connected) {
    Serial.println("[SETUP] ⚠ LSM6DS3 not found - will use fallback I2C reads");
  }

  // ========================================================================
  // CRITICAL: Setup Power Management (LSM6DS3 low-power or power-down)
  // ========================================================================

  Serial.println("[SETUP] Powering down internal sensors...");
  power_manager.begin();
  power_manager.powerDownInternalSensors(); // Handles both rise-to-wake and PDM

  // Pass LSM6DS3 reference to calibrator if connected
  if (lsm6ds3_connected) {
    imu_calibrator.setIMU(&lsm6ds3);
  }

  // ========================================================================
  // CRITICAL: If rise-to-wake enabled, load calibration from flash
  // ========================================================================

#if IMU_WAKE_ENABLED
  Serial.println("[SETUP] Rise-to-wake enabled. Loading calibration...");
  uint8_t saved_threshold;
  if (eeprom_manager.loadCalibration(saved_threshold)) {
    power_manager.updateIMUThreshold(saved_threshold);
    Serial.print("[SETUP] Loaded threshold: 0x");
    Serial.println(saved_threshold, HEX);
  } else {
    Serial.println("[SETUP] No saved calibration, using default");
  }
#else
  Serial.println("[SETUP] Rise-to-wake disabled");
#endif

  // ========================================================================
  // Initialize all modules
  // ========================================================================

  // Gesture detection
  Serial.println("[SETUP] Initializing gesture detector...");
  gesture_detector.begin();
  if (lsm6ds3_connected) {
    gesture_detector.setIMU(&lsm6ds3);
  }

  // LEDs
  Serial.println("[SETUP] Initializing LED ring...");
  led_controller.begin();
  led_controller.setRuntimeConfig(&runtime_config);
  led_controller.fillAll(COLOR_INFO);
  delay(100);
  led_controller.clear();
  led_controller.show();

  // Haptic motor
  Serial.println("[SETUP] Initializing haptic motor...");
  haptic.begin();

  // BLE
  Serial.println("[SETUP] Initializing BLE...");
  ble_handler.begin();
  led_controller.setBLEActive(true);

  // Setup BLE callbacks
  ble_handler.onHapticRX(onBLEHapticRX);
  ble_handler.onBearingUpdate(onBLEBearingUpdate);
  ble_handler.onDistanceUpdate(onBLEDistanceUpdate);
  ble_handler.onConfigUpdate(onBLEConfigUpdate);
  ble_handler.onCalibStart(onBLECalibStart);
  ble_handler.onCalibEnd(onBLECalibEnd);
  ble_handler.onCalibCancel(onBLECalibCancel);
  ble_handler.onOTARequest(onBLEOTARequest);
  ble_handler.onTimeSync(onBLETimeSync); // NEW: real-time clock sync

  // State machine
  Serial.println("[SETUP] Initializing state machine...");
  state_machine.begin();

// ========================================================================
// Setup interrupts for waking from DEEP_SLEEP
// ========================================================================

// D8: Button tap (commented out due to PCB dielectric noise latching pin HIGH)
// pinMode(PIN_BUTTON_TOUCH, INPUT_PULLDOWN);
// attachInterrupt(digitalPinToInterrupt(PIN_BUTTON_TOUCH), onButtonWakeup,
// RISING);

// IMU INT1: Rise-to-wake (if enabled)
#if IMU_WAKE_ENABLED
  pinMode(PIN_IMU_INT1, INPUT);
  attachInterrupt(digitalPinToInterrupt(PIN_IMU_INT1), onMotionWakeup, RISING);
  Serial.println("[SETUP] Motion wake interrupt attached");
#endif

  // Turn on LED ring (out of deep sleep after setup)
  led_controller.setPower(true);

  // Transition to initial state based on BLE connection
  if (ble_handler.isConnected()) {
    state_machine.transitionTo(STATE_CLOCK_CONNECTED);
  } else {
    state_machine.transitionTo(STATE_CLOCK_DISCONNECTED);
  }

  Serial.println("[SETUP] Complete! Starting main loop...\n");

  loop_last_ms = millis();
}

// ============================================================================
// MAIN LOOP
// ============================================================================

void loop() {
  uint32_t now_ms = millis();

  // Maintain ~10ms loop interval
  if ((now_ms - loop_last_ms) < LOOP_INTERVAL_MS) {
    return; // Not time yet
  }
  loop_last_ms = now_ms;

  // Process deferred config updates in main thread to prevent FreeRTOS stack
  // overflow/deadlocks
  if (config_update_pending) {
    config_update_pending = false;
    const char *json = ble_handler.getConfigJson();
    if (json && json[0] != '\0') {
      runtime_config.updateFromJson(json);
      Serial.println("[CONFIG] Config parsed and applied to RAM");

      // If wake threshold changed, apply it immediately
      const RuntimeConfig &cfg = runtime_config.getConfig();
      static uint8_t last_applied_threshold = 0xFF; // Start with invalid value
      if (last_applied_threshold != cfg.wakeThreshold) {
        last_applied_threshold = cfg.wakeThreshold;
        power_manager.updateIMUThreshold(cfg.wakeThreshold);
      }

      // Start debounce timer for saving to flash (3 seconds idle)
      last_config_change_ms = millis();
      config_save_pending = true;
    }
  }

  // Save config to flash after 3 seconds of stability (no new config changes
  // received)
  if (config_save_pending && (now_ms - last_config_change_ms >= 3000)) {
    config_save_pending = false;
    runtime_config.saveToFlash();
    Serial.println("[CONFIG] Deferred config saved to flash");
  }

  // Print state transitions for debugging
  static State last_printed_state = STATE_DEEP_SLEEP;
  State cur_state = state_machine.getCurrentState();
  if (cur_state != last_printed_state) {
    Serial.print("\n[STATE] Transition: ");
    Serial.print(state_machine.getStateName(last_printed_state));
    Serial.print(" -> ");
    Serial.println(state_machine.getStateName(cur_state));
    last_printed_state = cur_state;
  }

  // ========================================================================
  // UPDATE PERIPHERALS
  // ========================================================================

  gesture_detector.setThreshold(runtime_config.getConfig().gyroThreshold);
  gesture_detector.update(now_ms);
  compass.update();
  ble_handler.update();
  power_manager.update();
  led_controller.update(now_ms);
  haptic.update(now_ms);

  // ========================================================================
  // READ INPUTS & SERIAL SIMULATOR
  // ========================================================================

  GestureType gesture = gesture_detector.getGesture();

  // Notify app of gyro flick detection for live calibration feedback (0xFE = Gyro Flick)
  if (gesture != GESTURE_NONE && ble_connected) {
    ble_handler.notifyCalibStatus(0xFE, 0xFE);
  }

  // Read Serial inputs to simulate button gestures (bypass dielectric hardware
  // issue)
  static GestureType simulated_gesture = GESTURE_NONE;
  while (Serial.available() > 0) {
    char c = Serial.read();
    if (c == 't' || c == 'T') {
      simulated_gesture = GESTURE_TAP_SIMPLE;
      Serial.println("\n[SERIAL CMD] Simulating: TAP SIMPLE");
    } else if (c == 'd' || c == 'D') {
      simulated_gesture = GESTURE_TAP_DOUBLE;
      Serial.println("\n[SERIAL CMD] Simulating: TAP DOUBLE");
    } else if (c == 's' || c == 'S') {
      simulated_gesture = GESTURE_PRESS_SHORT;
      Serial.println("\n[SERIAL CMD] Simulating: PRESS SHORT");
    } else if (c == 'l' || c == 'L') {
      simulated_gesture = GESTURE_PRESS_LONG;
      Serial.println("\n[SERIAL CMD] Simulating: PRESS LONG");
    } else if (c == '+') {
      uint8_t curr = runtime_config.getConfig().brightnessPercent;
      if (curr <= 95)
        curr += 5;
      else
        curr = 100;
      runtime_config.setBrightnessPercent(curr);
      Serial.print("\n[SERIAL CMD] Brillo ajustado a: ");
      Serial.print(curr);
      Serial.println("%");
    } else if (c == '-') {
      uint8_t curr = runtime_config.getConfig().brightnessPercent;
      if (curr >= 5)
        curr -= 5;
      else
        curr = 0;
      runtime_config.setBrightnessPercent(curr);
      Serial.print("\n[SERIAL CMD] Brillo ajustado a: ");
      Serial.print(curr);
      Serial.println("%");
    } else if (c == 'r' || c == 'R') {
      runtime_config.resetDefaults();
      runtime_config.saveToFlash();
      Serial.println("\n[SERIAL CMD] Configuración restablecida a fábrica "
                     "(Brillo = 15%).");
    }
  }

  if (simulated_gesture != GESTURE_NONE) {
    gesture = simulated_gesture;
    simulated_gesture = GESTURE_NONE; // Reset
  }

  ble_connected = ble_handler.isConnected();
  battery_percent = power_manager.getBatteryPercent();

  // Update battery overlay
  if (power_manager.isLowBattery()) {
    state_machine.setLowBatteryActive(true);
  } else {
    state_machine.setLowBatteryActive(false);
  }

// Force deep sleep if critical battery (unless disabled for testing)
#if !DEBUG_DISABLE_DEEP_SLEEP
  if (power_manager.isCriticalBattery()) {
    state_machine.transitionTo(STATE_DEEP_SLEEP);
  }
#endif

  // ========================================================================
  // STATE MACHINE UPDATE
  // ========================================================================

#if IMU_WAKE_ENABLED
  if (motion_detected_flag) {
    motion_detected_flag = false;
    Serial.println("[MOTION] Wake gesture detected (INT1 RISING)");
    if (ble_connected) {
      ble_handler.notifyCalibStatus(
          0xFF, 0xFF); // 0xFF signifies manual motion detection
    }
  }
#endif

  state_machine.update(now_ms);

  // ========================================================================
  // STATE-SPECIFIC LOGIC & TRANSITIONS
  // ========================================================================

  switch (state_machine.getCurrentState()) {

  case STATE_DEEP_SLEEP:
    handleStateDeepSleep(gesture);
    break;

  case STATE_WAKING_UP:
    handleStateWakingUp();
    break;

  case STATE_CLOCK_CONNECTED:
    handleStateClockConnected(gesture, now_ms);
    break;

  case STATE_CLOCK_DISCONNECTED:
    handleStateClockDisconnected(gesture, now_ms);
    break;

  case STATE_RADAR_MODE:
    handleStateRadarMode(gesture);
    break;

  case STATE_DISTANCE_MODE:
    handleStateDistanceMode(gesture);
    break;

  case STATE_HAPTIC_TX:
    handleStateHapticTX();
    break;

  case STATE_HAPTIC_RX:
    handleStateHapticRX(gesture);
    break;

  case STATE_ERROR_NO_GPS:
    handleStateErrorNoGPS();
    break;

  case STATE_CALIBRATION_MODE:
    handleStateCalibration(gesture, now_ms);
    break;

  case STATE_OTA_MODE:
    handleStateOTAMode();
    break;

  default:
    break;
  }

  // ========================================================================
  // PERIODIC DEBUG OUTPUT (every 2 seconds)
  // ========================================================================

  static uint32_t debug_last_ms = 0;
  /*if ((now_ms - debug_last_ms) > 2000) {
    debug_last_ms = now_ms;
    debugPrintStatus();
  }*/
}

// ============================================================================
// STATE HANDLERS
// ============================================================================

void handleStateDeepSleep(GestureType gesture) {
  // If we just entered DEEP_SLEEP, configure IMU for Rise-to-Wake and clear
  // wake sources
  if (state_machine.stateChanged()) {
    last_wake_source = WAKE_SOURCE_NONE;
    Serial.println("[POWER] Entering DEEP_SLEEP state. Configuring IMU for "
                   "Rise-to-Wake...");
#if IMU_WAKE_ENABLED
    power_manager.setupIMUForRiseToWake();
#endif

    // Safety pull-downs for MOSFETs to prevent floating gate issues
    pinMode(PIN_MOTOR, OUTPUT);
    digitalWrite(PIN_MOTOR, LOW);
    pinMode(PIN_LED_POWER, OUTPUT);
    digitalWrite(PIN_LED_POWER, LOW);
  }

  // Turn off all peripherals
  led_controller.setPower(false);
  haptic.stopMotor();

  // Wait for button tap or motion to wake (via interrupts)
  if (last_wake_source != WAKE_SOURCE_NONE) {
    Serial.print("[POWER] Wake source detected: ");
    Serial.println(last_wake_source == WAKE_SOURCE_MOTION ? "MOTION" : "TAP");

    // Clear wake source and wake up
    last_wake_source = WAKE_SOURCE_NONE;
    state_machine.transitionTo(STATE_WAKING_UP);
    return;
  }

  // Backup: Software button tap check if interrupt didn't fire
  if (gesture == GESTURE_TAP_SIMPLE) {
    state_machine.transitionTo(STATE_WAKING_UP);
    return;
  }

// Put CPU to sleep until next interrupt (unless disabled for testing)
#if !DEBUG_DISABLE_DEEP_SLEEP
  power_manager.enterDeepSleep();
#else
  delay(10); // Don't hog CPU in debug stay-awake loop
#endif
}

void handleStateWakingUp() {
  // Power on LED ring
  led_controller.setPower(true);

  // Clear any leftover gestures
  gesture_detector.reset();

  // Re-initialize LSM6DS3 for normal gesture detection
  if (lsm6ds3_connected) {
    Serial.println("[POWER] Waking up. Restoring IMU to normal mode...");
    lsm6ds3.begin();
  }

  // Check BLE connection and transition accordingly
  if (ble_handler.isConnected()) {
    state_machine.transitionTo(STATE_CLOCK_CONNECTED);
  } else {
    state_machine.transitionTo(STATE_CLOCK_DISCONNECTED);
  }
}

void handleStateClockConnected(GestureType gesture, uint32_t now_ms) {
  static uint32_t last_time_update_ms = 0;

  // Update clock display every 33ms (~30 Hz) to reduce power sags and BLE
  // collisions
  if ((now_ms - last_time_update_ms) >= 33) {
    last_time_update_ms = now_ms;

    // Synchronize epoch seconds and millis_part to eliminate second hand phase
    // jumps
    uint64_t total_ms;
    if (time_synced) {
      total_ms = (uint64_t)unix_base_ts * 1000 + (now_ms - millis_at_sync);
    } else {
      total_ms = now_ms;
    }

    uint32_t total_secs = total_ms / 1000;
    uint8_t seconds = total_secs % 60;
    uint8_t minutes = (total_secs / 60) % 60;
    uint8_t hours = (total_secs / 3600) % 24;
    uint16_t millis_part = total_ms % 1000;

    led_controller.updateClockTime(hours, minutes, seconds, millis_part);
    led_controller.showClock(true); // connected = white colors
  }

  // Handle gestures
  switch (gesture) {
  case GESTURE_TAP_SIMPLE:
    // Reset timer (stay awake longer)
    state_machine.resetTimer(TIMER_CLOCK_TIMEOUT_MS);
    break;

  case GESTURE_TAP_DOUBLE:
    // Send haptic to pareja
    state_machine.transitionTo(STATE_HAPTIC_TX);
    break;

  case GESTURE_PRESS_SHORT:
    // Toggle to RADAR_MODE
    state_machine.transitionTo(STATE_RADAR_MODE);
    break;

  case GESTURE_PRESS_LONG:
    // Force deep sleep
    state_machine.transitionTo(STATE_DEEP_SLEEP);
    break;

  default:
    break;
  }

  // Check BLE disconnection
  if (!ble_handler.isConnected()) {
    state_machine.transitionTo(STATE_CLOCK_DISCONNECTED);
  }

  // Notify battery periodically
  static uint32_t battery_notify_last_ms = 0;
  if ((now_ms - battery_notify_last_ms) > 10000) {
    battery_notify_last_ms = now_ms;
    ble_handler.notifyBattery(battery_percent);
  }
}

void handleStateClockDisconnected(GestureType gesture, uint32_t now_ms) {
  static uint32_t last_time_update_ms = 0;

  // Update clock display every 500ms (~2 Hz) to give BLE advertising plenty of
  // idle time
  if ((now_ms - last_time_update_ms) >= 500) {
    last_time_update_ms = now_ms;

    // Synchronize epoch seconds and millis_part to eliminate second hand phase
    // jumps
    uint64_t total_ms;
    if (time_synced) {
      total_ms = (uint64_t)unix_base_ts * 1000 + (now_ms - millis_at_sync);
    } else {
      total_ms = now_ms;
    }

    uint32_t total_secs = total_ms / 1000;
    uint8_t seconds = total_secs % 60;
    uint8_t minutes = (total_secs / 60) % 60;
    uint8_t hours = (total_secs / 3600) % 24;
    // Set millis_part to 0 to disable smooth interpolation while disconnected
    uint16_t millis_part = 0;

    led_controller.updateClockTime(hours, minutes, seconds, millis_part);
    led_controller.showClock(false); // disconnected = blue colors
  }

  // Handle gestures (limited functionality without BLE)
  switch (gesture) {
  case GESTURE_TAP_SIMPLE:
    state_machine.resetTimer(TIMER_CLOCK_TIMEOUT_MS);
    break;

  case GESTURE_TAP_DOUBLE:
    // Ignored (no BLE connection)
    break;

  case GESTURE_PRESS_SHORT:
    // Can't go to RADAR without BLE; show error
    state_machine.transitionTo(STATE_ERROR_NO_GPS);
    break;

  case GESTURE_PRESS_LONG:
    state_machine.transitionTo(STATE_DEEP_SLEEP);
    break;

  default:
    break;
  }

  // Check if BLE reconnected
  if (ble_handler.isConnected()) {
    state_machine.transitionTo(STATE_CLOCK_CONNECTED);
  }
}

void handleStateRadarMode(GestureType gesture) {
  static uint32_t last_update = 0;
  uint32_t now = millis();

  if (now - last_update >= 50) {
    last_update = now;
    // Show bearing as single LED pointing toward pareja, adjusting for watch
    // compass heading
    float relative_bearing =
        current_bearing - (compass.isConnected() ? compass.getHeading() : 0.0f);
    relative_bearing = fmod(relative_bearing + 360.0f, 360.0f);
    led_controller.showRadar(relative_bearing);
  }

  // Notify app only once when entering RADAR_MODE (not every loop)
  static bool radar_notified = false;
  if (state_machine.stateChanged()) {
    radar_notified = false;
  }
  if (!radar_notified) {
    ble_handler.notifyRadarModeActive(true);
    radar_notified = true;
  }

  switch (gesture) {
  case GESTURE_TAP_SIMPLE:
    state_machine.resetTimer(TIMER_RADAR_TIMEOUT_MS);
    break;

  case GESTURE_TAP_DOUBLE:
    // Toggle to DISTANCE_MODE
    radar_notified = false;
    ble_handler.notifyRadarModeActive(false);
    state_machine.transitionTo(STATE_DISTANCE_MODE);
    break;

  case GESTURE_PRESS_SHORT:
    // Toggle back to CLOCK
    radar_notified = false;
    ble_handler.notifyRadarModeActive(false);
    state_machine.transitionTo(STATE_CLOCK_CONNECTED);
    break;

  case GESTURE_PRESS_LONG:
    radar_notified = false;
    ble_handler.notifyRadarModeActive(false);
    state_machine.transitionTo(STATE_DEEP_SLEEP);
    break;

  default:
    break;
  }
}

void handleStateDistanceMode(GestureType gesture) {
  static uint32_t last_update = 0;
  uint32_t now = millis();

  if (now - last_update >= 50) {
    last_update = now;
    // Show distance as LEDs filling from center
    led_controller.showDistance(current_distance);
  }

  switch (gesture) {
  case GESTURE_TAP_SIMPLE:
    state_machine.resetTimer(TIMER_RADAR_TIMEOUT_MS);
    break;

  case GESTURE_TAP_DOUBLE:
    // Toggle to RADAR_MODE
    state_machine.transitionTo(STATE_RADAR_MODE);
    break;

  case GESTURE_PRESS_SHORT:
    // Toggle back to CLOCK
    state_machine.transitionTo(STATE_CLOCK_CONNECTED);
    break;

  case GESTURE_PRESS_LONG:
    state_machine.transitionTo(STATE_DEEP_SLEEP);
    break;

  default:
    break;
  }
}

void handleStateHapticTX() {
  static bool pattern_started = false;

  if (state_machine.stateChanged()) {
    pattern_started = false;
  }

  if (!pattern_started) {
    ble_handler.notifyHapticTX();
    if (runtime_config.getConfig().hapticPatternIndex ==
        1) {                                      // 1 = only partner
      haptic.playPattern(HAPTIC_PATTERN_BATTERY); // confirm with a short pulse
    } else {
      haptic.playPattern(HAPTIC_PATTERN_RX); // long vibration pattern
    }

    uint8_t brightness_pct = runtime_config.getConfig().brightnessPercent;

    uint8_t base_brightness = (brightness_pct * 255) / 100;

    // Usar rosa (COLOR_HAPTIC_RX) escalado por brillo seguro
    led_controller.fillWithBrightness(COLOR_HAPTIC_RX, base_brightness);
    pattern_started = true;
  }

  // Return when haptic finishes
  if (!haptic.isVibrating() && pattern_started) {
    pattern_started = false;
    state_machine.transitionTo(STATE_CLOCK_CONNECTED);
  }
}

void handleStateHapticRX(GestureType gesture) {
  // Vibrate with haptic pattern + pink LED pulse
  static bool pattern_started = false;

  if (state_machine.stateChanged()) {
    pattern_started = false;
  }

  if (!pattern_started) {
    haptic.playPattern(HAPTIC_PATTERN_RX);
    led_controller.animateHapticRX();
    pattern_started = true;
  }

  // Tap to cancel
  if (gesture == GESTURE_TAP_SIMPLE) {
    haptic.stopPattern();
    pattern_started = false;
    state_machine.transitionTo(STATE_CLOCK_CONNECTED);
  }

  // Auto-return when haptic finishes
  if (!haptic.isVibrating() && pattern_started) {
    pattern_started = false;
    state_machine.transitionTo(STATE_CLOCK_CONNECTED);
  }
}

void handleStateErrorNoGPS() {
  static bool error_displayed = false;
  if (state_machine.stateChanged()) {
    error_displayed = false;
  }

  if (!error_displayed) {
    led_controller.errorNoGPS();
    haptic.stopMotor();
    error_displayed = true;
  }
}

void handleStateCalibration(GestureType gesture, uint32_t now_ms) {
  // NEW: Rise-to-wake calibration

  static bool calib_started = false;
  static uint8_t last_progress = 0xFF;

  if (state_machine.stateChanged()) {
    calib_started = false;
    last_progress = 0xFF;
  }

  uint8_t brightness_pct = runtime_config.getConfig().brightnessPercent;
  uint8_t base_brightness = (brightness_pct * 255) / 100;

  if (!calib_started) {
    imu_calibrator.begin();
    calib_started = true;
    led_controller.fillWithBrightness(COLOR_INFO, base_brightness);
  }

  // Update calibration
  imu_calibrator.update(now_ms);

  // Show progress on LEDs (filling from 1 to 12) and notify BLE only on changes
  uint8_t progress = imu_calibrator.getProgress();
  if (progress != last_progress) {
    last_progress = progress;

    int leds_to_fill = (progress * 12) / CALIBRATION_NUM_SAMPLES;
    led_controller.clear();
    for (int i = 0; i < leds_to_fill; i++) {
      led_controller.setLEDBrightness(i, COLOR_SUCCESS, base_brightness);
    }
    led_controller.show();

    // Notify app of progress
    ble_handler.notifyCalibStatus(progress, CALIBRATION_NUM_SAMPLES);
  }

  // Check if calibration finished
  if (!imu_calibrator.isActive()) {
    uint8_t threshold = imu_calibrator.getThreshold();

    // Save to flash
    eeprom_manager.saveCalibration(threshold);

    // Update IMU with new threshold
    power_manager.updateIMUThreshold(threshold);

    // Notify app
    ble_handler.notifyCalibThreshold(threshold);

    // Flash green success
    led_controller.fillWithBrightness(COLOR_SUCCESS, base_brightness);
    haptic.playPattern(HAPTIC_PATTERN_TX);

    // Return to clock
    calib_started = false;
    state_machine.transitionTo(STATE_CLOCK_CONNECTED);
  }

  // Tap to cancel
  if (gesture == GESTURE_TAP_SIMPLE) {
    imu_calibrator.cancel();
    calib_started = false;
    state_machine.transitionTo(STATE_CLOCK_CONNECTED);
  }
}

void handleStateOTAMode() {
  // Show LED progress animation while preparing
  static uint8_t ota_progress = 0;
  led_controller.updateOTAProgress(ota_progress);

  // Wait 1 second for the user to see the LEDs, then reboot into DFU
  static uint32_t ota_enter_time = 0;
  if (ota_enter_time == 0) {
    ota_enter_time = millis();
    haptic.playPattern(HAPTIC_PATTERN_TX); // Buzz to confirm
  }

  if ((millis() - ota_enter_time) > 1500) {
    // Enter DFU bootloader mode
    // The Adafruit nRF52 bootloader checks GPREGRET on boot:
    // 0xA8 = enter DFU mode (BLE OTA)
    Serial.println("[OTA] Entering DFU bootloader...");
    delay(100); // Flush serial

    NRF_POWER->GPREGRET = 0xA8;
    NVIC_SystemReset();
    // Device reboots here — won't reach this point
  }
}

// ============================================================================
// BLE CALLBACKS
// ============================================================================

void onBLEHapticRX() {
  // Mobile sent a haptic command
  if (state_machine.getCurrentState() != STATE_OTA_MODE &&
      state_machine.getCurrentState() != STATE_DEEP_SLEEP) {
    state_machine.transitionTo(STATE_HAPTIC_RX);
  }
}

void onBLEBearingUpdate(float bearing) { current_bearing = bearing; }

void onBLEDistanceUpdate(uint32_t distance_m) { current_distance = distance_m; }

void onBLECalibStart() {
  // App requested start calibration
  if (state_machine.getCurrentState() != STATE_DEEP_SLEEP) {
    state_machine.transitionTo(STATE_CALIBRATION_MODE);
  }
}

void onBLECalibEnd() {
  // App requested end calibration (might be premature)
  // Calibrator will decide to finalize or continue
  // This is mainly for UI: app button "Done"
}

void onBLECalibCancel() {
  // App requested cancel
  imu_calibrator.cancel();
  state_machine.transitionTo(STATE_CLOCK_CONNECTED);
}

void onBLEConfigUpdate() {
  // App sent a new config JSON via CONFIG_CHAR.
  // Set flag to process in the main thread (avoid slow filesystem write in BLE
  // interrupt context)
  config_update_pending = true;
}

void onBLEOTARequest() {
  // App requested OTA update
  Serial.println("[OTA] OTA update requested from app");
  state_machine.transitionTo(STATE_OTA_MODE);
}

void onBLETimeSync(uint32_t unix_ts) {
  // App sent current Unix timestamp → store sync point
  unix_base_ts = unix_ts;
  millis_at_sync = millis();
  time_synced = true;

  // Log human-readable time (UTC)
  uint8_t secs = unix_ts % 60;
  uint8_t mins = (unix_ts / 60) % 60;
  uint8_t hrs = (unix_ts / 3600) % 24;
  Serial.print("[TIME] Clock synced — UTC: ");
  if (hrs < 10)
    Serial.print('0');
  Serial.print(hrs);
  Serial.print(':');
  if (mins < 10)
    Serial.print('0');
  Serial.print(mins);
  Serial.print(':');
  if (secs < 10)
    Serial.print('0');
  Serial.println(secs);
}

// ============================================================================
// INTERRUPT HANDLERS
// ============================================================================

void onButtonWakeup() {
  // Interrupt from button (D8) during deep sleep
  last_wake_source = WAKE_SOURCE_TAP;
}

void onMotionWakeup() {
  // NEW: Interrupt from IMU (INT1) during deep sleep (rise-to-wake)
  last_wake_source = WAKE_SOURCE_MOTION;
  motion_detected_flag = true;
}

// ============================================================================
// DEBUG & UTILITIES
// ============================================================================

// DEBUG & UTILITIES
// ============================================================================

void debugPrintStatus() {
  static uint32_t debug_cycle = 0;
  debug_cycle++;

  Serial.println("\n========== DEBUG STATUS ==========");

  // ========================================================================
  // SYSTEM STATE
  // ========================================================================
  Serial.print("[SYSTEM] State: ");
  Serial.print(state_machine.getStateName());
  Serial.print(" | BLE: ");
  Serial.print(ble_connected ? "✓ CONNECTED" : "✗ DISCONNECTED");
  Serial.print(" | Battery: ");
  Serial.print(battery_percent);
  Serial.print("% | Timer: ");
  Serial.print(state_machine.getTimerRemaining());
  Serial.println("ms");

  // ========================================================================
  // INPUTS (Button, Gesture)
  // ========================================================================
  bool btn_pressed = digitalRead(PIN_BUTTON_TOUCH) == HIGH;
  Serial.print("[INPUTS] Button: ");
  Serial.print(btn_pressed ? "✓ PRESSED" : "- RELEASED");
  Serial.print(" | Last Gesture: ");
  GestureType last_gesture = gesture_detector.getGesture();
  switch (last_gesture) {
  case GESTURE_NONE:
    Serial.print("NONE");
    break;
  case GESTURE_TAP_SIMPLE:
    Serial.print("TAP");
    break;
  case GESTURE_TAP_DOUBLE:
    Serial.print("DOUBLE_TAP");
    break;
  case GESTURE_PRESS_SHORT:
    Serial.print("PRESS_SHORT");
    break;
  case GESTURE_PRESS_LONG:
    Serial.print("PRESS_LONG");
    break;
  default:
    Serial.print("?");
    break;
  }
  Serial.println();

  // ========================================================================
  // ACCELEROMETER (LSM6DS3 on Wire/Wire0)
  // ========================================================================
  Serial.print("[ACCEL] ");
  if (lsm6ds3_connected &&
      state_machine.getCurrentState() != STATE_DEEP_SLEEP) {
    uint32_t accel_start = millis();

    float accel_x = lsm6ds3.readFloatAccelX();
    float accel_y = lsm6ds3.readFloatAccelY();
    float accel_z = lsm6ds3.readFloatAccelZ();
    float gyro_x = lsm6ds3.readFloatGyroX();
    float gyro_y = lsm6ds3.readFloatGyroY();
    float gyro_z = lsm6ds3.readFloatGyroZ();
    float temp_c = lsm6ds3.readTempC();

    float magnitude =
        sqrtf(accel_x * accel_x + accel_y * accel_y + accel_z * accel_z);

    Serial.print("✓ A[");
    Serial.print(accel_x, 2);
    Serial.print(",");
    Serial.print(accel_y, 2);
    Serial.print(",");
    Serial.print(accel_z, 2);
    Serial.print("]g |M|=");
    Serial.print(magnitude, 2);
    Serial.print("g | G[");
    Serial.print(gyro_x, 1);
    Serial.print(",");
    Serial.print(gyro_y, 1);
    Serial.print(",");
    Serial.print(gyro_z, 1);
    Serial.print("]°/s | T=");
    Serial.print(temp_c, 1);
    Serial.print("°C");

    uint32_t accel_time = millis() - accel_start;
    Serial.print(" [" + String(accel_time) + "ms]");

    // Show INT1 pin state — HIGH = wake-up event pending / latched
    bool int1_active = digitalRead(PIN_IMU_INT1) == HIGH;
    if (int1_active) {
      Serial.print(" | ⚠ INT1=HIGH (wake event latched!)");
    }
  } else {
    if (!lsm6ds3_connected) {
      Serial.print("✗ Not connected");
    } else {
      Serial.print("- Disabled in DEEP_SLEEP");
    }
  }
  Serial.println();

  // ========================================================================
  // MAGNETOMETER (LIS3MDL on custom Wire pins D4/D5)
  // ========================================================================
  Serial.print("[COMPASS] ");
  if (compass.isConnected()) {
    Serial.print("✓ CONNECTED | ");
    Serial.print("Heading: ");
    Serial.print(compass.getHeading(), 1);
    Serial.print("° | ");
    Serial.print("Raw: X=");
    Serial.print(compass.getRawX(), 1);
    Serial.print(" ");
    Serial.print("Y=");
    Serial.print(compass.getRawY(), 1);
    Serial.print(" ");
    Serial.print("Z=");
    Serial.print(compass.getRawZ(), 1);
    if (compass.isCalibrating()) {
      Serial.print(" | ⚠ CALIBRATING");
    }
  } else {
    Serial.print("✗ NOT CONNECTED (using app bearing)");
  }
  Serial.println();

  // ========================================================================
  // BEARING & DISTANCE (from BLE / App)
  // ========================================================================
  Serial.print("[BLE DATA] Bearing: ");
  Serial.print(current_bearing, 1);
  Serial.print("° | Distance: ");
  Serial.print(current_distance / 1000.0f, 1);
  Serial.println(" km");

  // ========================================================================
  // POWER & ENERGY
  // ========================================================================
  Serial.print("[POWER] Battery: ");
  Serial.print(battery_percent);
  Serial.print("% | ");
  if (power_manager.isLowBattery()) {
    Serial.print("⚠ LOW_BATTERY");
  } else if (power_manager.isCriticalBattery()) {
    Serial.print("🔋 CRITICAL");
  } else {
    Serial.print("✓ OK");
  }
  Serial.print(" | Sleeping: ");
  Serial.print(power_manager.isAsleep() ? "YES" : "NO");
  // Show last wake source for debugging
  Serial.print(" | Last wake: ");
  switch (last_wake_source) {
  case WAKE_SOURCE_NONE:
    Serial.print("NONE");
    break;
  case WAKE_SOURCE_TAP:
    Serial.print("BUTTON");
    break;
  case WAKE_SOURCE_MOTION:
    Serial.print("MOTION(IMU)");
    break;
  default:
    Serial.print("?");
    break;
  }
  // Show INT1 raw pin state
  Serial.print(" | INT1=");
  Serial.print(digitalRead(PIN_IMU_INT1) ? "HIGH" : "LOW");
  Serial.println();

  // ========================================================================
  // TIMING STATS
  // ========================================================================
  Serial.print("[TIMING] Uptime: ");
  uint32_t uptime_s = millis() / 1000;
  uint16_t hours = uptime_s / 3600;
  uint8_t minutes = (uptime_s % 3600) / 60;
  uint8_t seconds = uptime_s % 60;
  Serial.print(hours);
  Serial.print("h ");
  Serial.print(minutes);
  Serial.print("m ");
  Serial.print(seconds);
  Serial.print("s");
  Serial.print(" | Debug Cycle: ");
  Serial.println(debug_cycle);

// ========================================================================
// CONFIGURATION & FLAGS
// ========================================================================
#if DEBUG_DISABLE_DEEP_SLEEP
  Serial.print("[CONFIG] ⚠ DEEP_SLEEP DISABLED ");
#else
  Serial.print("[CONFIG] - Deep sleep enabled ");
#endif

#if IMU_WAKE_ENABLED
  Serial.println("| ✓ Rise-to-wake ENABLED");
#else
  Serial.println("| - Rise-to-wake DISABLED");
#endif

  Serial.println("==================================\n");
}

// ============================================================================
// END OF FIRMWARE
// ============================================================================
