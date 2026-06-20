/*
  ============================================================================
  COUPLES WATCH — Firmware for Seeed Studio XIAO nRF52840 Sense
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

#include "config.h"
#include "state_machine.h"
#include "gesture.h"
#include "led_controller.h"
#include "compass.h"
#include "haptic.h"
#include "ble_handler.h"
#include "power.h"
#include "imu_calibrator.h"
#include "eeprom_manager.h"
#include "runtime_config.h"
#include <nrf_power.h>

// Arduino/nRF52840 libraries
#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_LSM6DS3TRC.h>
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

// Timing
uint32_t loop_last_ms = 0;
const uint32_t LOOP_INTERVAL_MS = 10;  // 100 Hz main loop

// State variables
bool ble_connected = false;
float current_bearing = 0;  // From BLE
uint32_t current_distance = 0;  // From BLE
uint8_t battery_percent = 100;
int last_wake_source = WAKE_SOURCE_NONE;

// ============================================================================
// SETUP
// ============================================================================

void setup() {
  // Initialize serial for debugging
  Serial.begin(115200);
  delay(1000);
  
  Serial.println("\n[SETUP] Couples Watch Firmware v1.2");
  Serial.println("[SETUP] Initializing...");
  
  // ========================================================================
  // CRITICAL: Initialize EEPROM/Flash first
  // ========================================================================
  
  Serial.println("[SETUP] Initializing EEPROM...");
  eeprom_manager.begin();
  
  Serial.println("[SETUP] Initializing runtime config...");
  runtime_config.begin();
  
  // ========================================================================
  // CRITICAL: Setup Power Management (LSM6DS3 low-power or power-down)
  // ========================================================================
  
  Serial.println("[SETUP] Powering down internal sensors...");
  power_manager.begin();
  power_manager.powerDownInternalSensors();  // Handles both rise-to-wake and PDM
  
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
  // Initialize I2C and external compass
  // ========================================================================
  
  Wire.setPins(PIN_COMPASS_SDA, PIN_COMPASS_SCL);
  Wire.begin();
  
  // ========================================================================
  // Initialize all modules
  // ========================================================================
  
  // Gesture detection
  Serial.println("[SETUP] Initializing gesture detector...");
  gesture_detector.begin();
  
  // LEDs
  Serial.println("[SETUP] Initializing LED ring...");
  led_controller.begin();
  led_controller.setRuntimeConfig(&runtime_config);
  led_controller.fillAll(COLOR_INFO);
  delay(100);
  led_controller.clear();
  
  // Compass
  Serial.println("[SETUP] Initializing compass (LIS3MDL)...");
  compass.begin();
  
  // Haptic motor
  Serial.println("[SETUP] Initializing haptic motor...");
  haptic.begin();
  
  // BLE
  Serial.println("[SETUP] Initializing BLE...");
  ble_handler.begin();
  
  // Setup BLE callbacks
  ble_handler.onHapticRX(onBLEHapticRX);
  ble_handler.onBearingUpdate(onBLEBearingUpdate);
  ble_handler.onDistanceUpdate(onBLEDistanceUpdate);
  ble_handler.onConfigUpdate(onBLEConfigUpdate);
  ble_handler.onCalibStart(onBLECalibStart);
  ble_handler.onCalibEnd(onBLECalibEnd);
  ble_handler.onCalibCancel(onBLECalibCancel);
  ble_handler.onOTARequest(onBLEOTARequest);
  
  // State machine
  Serial.println("[SETUP] Initializing state machine...");
  state_machine.begin();
  
  // ========================================================================
  // Setup interrupts for waking from DEEP_SLEEP
  // ========================================================================
  
  // D8: Button tap
  pinMode(PIN_BUTTON_TOUCH, INPUT);
  attachInterrupt(digitalPinToInterrupt(PIN_BUTTON_TOUCH), onButtonWakeup, RISING);
  
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
    return;  // Not time yet
  }
  loop_last_ms = now_ms;
  
  // ========================================================================
  // UPDATE PERIPHERALS
  // ========================================================================
  
  gesture_detector.update(now_ms);
  compass.update();
  ble_handler.update();
  power_manager.update();
  led_controller.update(now_ms);
  haptic.update(now_ms);
  
  // ========================================================================
  // READ INPUTS
  // ========================================================================
  
  GestureType gesture = gesture_detector.getGesture();
  ble_connected = ble_handler.isConnected();
  battery_percent = power_manager.getBatteryPercent();
  
  // Update battery overlay
  if (power_manager.isLowBattery()) {
    state_machine.setLowBatteryActive(true);
  } else {
    state_machine.setLowBatteryActive(false);
  }
  
  // Force deep sleep if critical battery
  if (power_manager.isCriticalBattery()) {
    state_machine.transitionTo(STATE_DEEP_SLEEP);
  }
  
  // ========================================================================
  // STATE MACHINE UPDATE
  // ========================================================================
  
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
  if ((now_ms - debug_last_ms) > 2000) {
    debug_last_ms = now_ms;
    debugPrintStatus();
  }
}

// ============================================================================
// STATE HANDLERS
// ============================================================================

void handleStateDeepSleep(GestureType gesture) {
  // Turn off all peripherals
  led_controller.setPower(false);
  haptic.stopMotor();
  
  // Wait for button tap to wake
  if (gesture == GESTURE_TAP_SIMPLE) {
    state_machine.transitionTo(STATE_WAKING_UP);
  }
}

void handleStateWakingUp() {
  // Power on LED ring
  led_controller.setPower(true);
  
  // Check BLE connection and transition accordingly
  if (ble_handler.isConnected()) {
    state_machine.transitionTo(STATE_CLOCK_CONNECTED);
  } else {
    state_machine.transitionTo(STATE_CLOCK_DISCONNECTED);
  }
}

void handleStateClockConnected(GestureType gesture, uint32_t now_ms) {
  static uint32_t last_time_update_ms = 0;
  
  // Update clock display every 100ms
  if ((now_ms - last_time_update_ms) >= 100) {
    last_time_update_ms = now_ms;
    
    // Get current time from system
    uint32_t epoch = millis() / 1000;
    uint8_t seconds = epoch % 60;
    uint8_t minutes = (epoch / 60) % 60;
    uint8_t hours = (epoch / 3600) % 24;
    uint16_t millis_part = millis() % 1000;
    
    led_controller.updateClockTime(hours, minutes, seconds, millis_part);
    led_controller.showClock(true);  // connected = white colors
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
  
  // Update clock display every 100ms (same as connected but different colors)
  if ((now_ms - last_time_update_ms) >= 100) {
    last_time_update_ms = now_ms;
    
    uint32_t epoch = millis() / 1000;
    uint8_t seconds = epoch % 60;
    uint8_t minutes = (epoch / 60) % 60;
    uint8_t hours = (epoch / 3600) % 24;
    uint16_t millis_part = millis() % 1000;
    
    led_controller.updateClockTime(hours, minutes, seconds, millis_part);
    led_controller.showClock(false);  // disconnected = blue colors
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
  // Show bearing as single LED pointing toward pareja
  led_controller.showRadar(current_bearing);
  
  // Notify app only once when entering RADAR_MODE (not every loop)
  static bool radar_notified = false;
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
  // Show distance as LEDs filling from center
  led_controller.showDistance(current_distance);
  
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
  // Flash white twice + send BLE notification
  static bool tx_sent = false;
  
  if (!tx_sent) {
    ble_handler.notifyHapticTX();
    led_controller.fillAll(COLOR_SUCCESS);
    led_controller.show();
    tx_sent = true;
  }
  
  // After animation, return to previous state
  if (state_machine.getTimerRemaining() == 0) {
    tx_sent = false;
    state_machine.transitionTo(STATE_CLOCK_CONNECTED);
  }
}

void handleStateHapticRX(GestureType gesture) {
  // Vibrate with haptic pattern + pink LED pulse
  static bool pattern_started = false;
  
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
  // 3 rapid red pulses
  led_controller.errorNoGPS();
  haptic.stopMotor();
}

void handleStateCalibration(GestureType gesture, uint32_t now_ms) {
  // NEW: Rise-to-wake calibration
  
  static bool calib_started = false;
  
  if (!calib_started) {
    imu_calibrator.begin();
    calib_started = true;
    led_controller.fillAll(COLOR_INFO);
    led_controller.show();
  }
  
  // Update calibration
  imu_calibrator.update(now_ms);
  
  // Show progress on LEDs (filling from 1 to 12)
  uint8_t progress = imu_calibrator.getProgress();
  int leds_to_fill = (progress * 12) / CALIBRATION_NUM_SAMPLES;
  
  led_controller.clear();
  for (int i = 0; i < leds_to_fill; i++) {
    led_controller.setLEDBrightness(i, COLOR_SUCCESS, 200);
  }
  led_controller.show();
  
  // Notify app of progress
  ble_handler.notifyCalibStatus(progress, CALIBRATION_NUM_SAMPLES);
  
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
    led_controller.fillAll(COLOR_SUCCESS);
    led_controller.show();
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
    haptic.playPattern(HAPTIC_PATTERN_TX);  // Buzz to confirm
  }
  
  if ((millis() - ota_enter_time) > 1500) {
    // Enter DFU bootloader mode
    // The Adafruit nRF52 bootloader checks GPREGRET on boot:
    // 0xA8 = enter DFU mode (BLE OTA)
    Serial.println("[OTA] Entering DFU bootloader...");
    delay(100);  // Flush serial
    
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

void onBLEBearingUpdate(float bearing) {
  current_bearing = bearing;
}

void onBLEDistanceUpdate(uint32_t distance_m) {
  current_distance = distance_m;
}

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
  // App sent a new config JSON via CONFIG_CHAR
  // Read the string value from the BLE characteristic
  // The config_char is a BLEStringCharacteristic — we can get the value
  // via ble_handler's internal access. For now, use a simpler approach:
  // The callback is fired after config_char.written(), so we read it.
  
  // Note: We access the config JSON through ble_handler's getConfigJson()
  const char* json = ble_handler.getConfigJson();
  if (json && json[0] != '\0') {
    runtime_config.updateFromJson(json);
    Serial.println("[CONFIG] Config updated from app");
    
    // If wake threshold changed, apply it immediately
    const RuntimeConfig& cfg = runtime_config.getConfig();
    power_manager.updateIMUThreshold(cfg.wakeThreshold);
  }
}

void onBLEOTARequest() {
  // App requested OTA update
  Serial.println("[OTA] OTA update requested from app");
  state_machine.transitionTo(STATE_OTA_MODE);
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
}

// ============================================================================
// DEBUG & UTILITIES
// ============================================================================

void debugPrintStatus() {
  Serial.print("[STATUS] State: ");
  Serial.print(state_machine.getStateName());
  Serial.print(" | BLE: ");
  Serial.print(ble_connected ? "CONNECTED" : "DISCONNECTED");
  Serial.print(" | Battery: ");
  Serial.print(battery_percent);
  Serial.print("% | Bearing: ");
  Serial.print(current_bearing, 1);
  Serial.print("° | Distance: ");
  Serial.print(current_distance / 1000.0f, 1);
  Serial.println(" km");
}

// ============================================================================
// END OF FIRMWARE
// ============================================================================
