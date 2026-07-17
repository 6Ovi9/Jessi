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

  Version: 2.2
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

// Pending flags for BLE callbacks (to execute safely in main loop)
volatile bool ble_calib_start_pending = false;
volatile bool ble_threshold_write_pending = false;
volatile uint8_t ble_new_threshold = IMU_WAKE_UP_THS_DEFAULT;
const uint32_t LOOP_INTERVAL_MS = 10; // 100 Hz main loop

// State variables
bool ble_connected = false;
// BUG-003: BLE callbacks write these from ISR context — must be volatile so the
// compiler never caches them in a register across the ISR boundary.
volatile float current_bearing = 0;
volatile uint32_t current_distance = 0;
uint8_t battery_percent = 100;
// BUG-001: Written from ISR (onButtonWakeup / onMotionWakeup) — must be volatile.
volatile int last_wake_source = WAKE_SOURCE_NONE;
bool lsm6ds3_connected = false;
volatile bool motion_detected_flag = false;
volatile bool config_update_pending = false;
// BUG-006: Deferred cancel flag — set from BLE ISR, processed safely in loop().
volatile bool calib_cancel_pending = false;
uint32_t last_config_change_ms = 0;
bool config_save_pending = false;
volatile bool haptic_rx_pending = false;
volatile bool calib_start_pending = false;
volatile bool ota_request_pending = false;
GestureType simulated_gesture = GESTURE_NONE;
bool sleep_entry_done = false;

// ── Time sync (Unix timestamp received from app via BLE) ────────────────────
// BUG-003: All variables must be written atomically.
volatile bool time_synced = false;
volatile uint32_t unix_base_ts = 0;
volatile int32_t tz_offset_s = 0;
volatile uint32_t millis_at_sync = 0;

/// Return current Unix timestamp (milliseconds since epoch).
/// Falls back to uptime if no sync has been received yet.
inline uint64_t getRealEpochMs() {
  bool local_synced;
  uint32_t local_base, local_millis;
  noInterrupts();
  local_synced = time_synced;
  local_base = unix_base_ts;
  local_millis = millis_at_sync;
  interrupts();
  
  if (local_synced) {
    uint64_t epoch_ms = ((uint64_t)local_base) * 1000ULL
                      + (uint64_t)(millis() - local_millis);
    return epoch_ms;
  }
  return millis();
}

/// Return current Unix timestamp (seconds since epoch).
/// Falls back to uptime if no sync has been received yet.
/// BUG-002: Cast to uint64_t BEFORE multiplying. A 2024 Unix timestamp (~1.7e9)
/// multiplied by 1000 as uint32_t overflows, producing a garbage clock time.
inline uint32_t getRealEpoch() {
  bool local_synced;
  uint32_t local_base, local_millis;
  noInterrupts();
  local_synced = time_synced;
  local_base = unix_base_ts;
  local_millis = millis_at_sync;
  interrupts();
  
  if (local_synced) {
    uint64_t epoch_ms = ((uint64_t)local_base) * 1000ULL
                      + (uint64_t)(millis() - local_millis);
    return (uint32_t)(epoch_ms / 1000ULL);
  }
  return millis() / 1000;
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
  Serial.println("[SETUP] Nexus Halo Firmware v2.3.0");

  // Turn off XIAO internal status LEDs (Active LOW) to prevent battery drain
  pinMode(LED_RED, OUTPUT);
  digitalWrite(LED_RED, HIGH);
  pinMode(LED_GREEN, OUTPUT);
  digitalWrite(LED_GREEN, HIGH);
  pinMode(LED_BLUE, OUTPUT);
  digitalWrite(LED_BLUE, HIGH);

  // ========================================================================
  // CRITICAL: Power on internal IMU first to stabilize I2C lines
  // ========================================================================
  Serial.println("[SETUP] Powering on internal IMU...");
  pinMode(PIN_LSM6DS3TR_C_POWER, OUTPUT);
  digitalWrite(PIN_LSM6DS3TR_C_POWER, HIGH);
  delay(300); // Increased from 50ms to 300ms to prevent 3.3V rail brownout when compass initializes

  // ========================================================================
  // CRITICAL: Initialize EEPROM/Flash first so LittleFS is ready
  // ========================================================================
  Serial.println("[SETUP] Initializing InternalFS...");
  if (!InternalFS.begin()) {
    Serial.println("[SETUP] ✗ InternalFS Mount Failed!");
  }
  
  Serial.println("[SETUP] Initializing EEPROM...");
  eeprom_manager.begin();

  Serial.println("[SETUP] Initializing runtime config...");
  runtime_config.begin();

  // ========================================================================
  // DIAGNOSTIC I2C SCANNER IN MAIN SETUP (Mimics compass_diagnostic.ino)
  // ========================================================================
  Serial.println("\n--- RUNNING IN-SETUP I2C SCANNER (10kHz) ---");
  pinMode(PIN_COMPASS_SDA, INPUT_PULLUP);
  pinMode(PIN_COMPASS_SCL, INPUT_PULLUP);
  delay(10);
  
  if (digitalRead(PIN_COMPASS_SDA) == LOW || digitalRead(PIN_COMPASS_SCL) == LOW) {
    Serial.println("  [SCAN] ✗ I2C bus (D4/D5) is stuck LOW! Missing pull-ups or hardware fault. Skipping scan.");
  } else {
    Wire.setPins(PIN_COMPASS_SDA, PIN_COMPASS_SCL);
    Wire.begin();
    Wire.setTimeout(3);
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
    Wire.end(); // CRITICAL: Release hardware TWI
    NRF_TWIM0->PSEL.SCL = 0xFFFFFFFF; // Disconnect TWI hardware pins
    NRF_TWIM0->PSEL.SDA = 0xFFFFFFFF;
    pinMode(PIN_COMPASS_SDA, INPUT_PULLUP);
    pinMode(PIN_COMPASS_SCL, INPUT_PULLUP);
    delay(10);
  }
  Serial.println("------------------------------------\n");

  Serial.println("[SETUP] Initializing compass (LIS3MDL)...");
  compass.begin();

  // Safety pull-downs for MOSFETs to prevent floating gate issues
  pinMode(PIN_MOTOR, OUTPUT);
  digitalWrite(PIN_MOTOR, LOW);
  pinMode(PIN_LED_POWER, OUTPUT);
  digitalWrite(PIN_LED_POWER, LOW);

  // Configure unused pins as INPUT_PULLDOWN to prevent leakage current from floating inputs
  pinMode(D0, INPUT_PULLDOWN);
  pinMode(D1, INPUT_PULLDOWN);
  pinMode(D2, INPUT_PULLDOWN);
  pinMode(D3, INPUT_PULLDOWN);
  pinMode(D6, INPUT_PULLDOWN);

  // ========================================================================
  // Initialize internal I2C bus (Wire1 → LSM6DS3 IMU)
  // ========================================================================

  Serial.println("[SETUP] Initializing I2C buses (Wire & Wire1)...");
  Wire1.begin(); // Internal bus for LSM6DS3
  Wire1.setTimeout(3);
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
  static uint8_t saved_threshold = IMU_WAKE_UP_THS_DEFAULT;
  bool has_saved = eeprom_manager.loadCalibration(saved_threshold);
  if (has_saved) {
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

#if IMU_WAKE_ENABLED
  if (has_saved) {
    if (ble_handler.beginOk()) {
      ble_handler.notifyCalibThreshold(saved_threshold);
    }
  }
#endif

  if (ble_handler.beginOk()) {
    char json_buf[256];
    const RuntimeConfig& c = runtime_config.getConfig();
    snprintf(json_buf, sizeof(json_buf), "{\"ct\":%d,\"st\":%d,\"chc\":\"%08X\",\"cmc\":\"%08X\",\"csc\":\"%08X\",\"chd\":\"%08X\",\"cmd\":\"%08X\",\"csd\":\"%08X\",\"br\":%d,\"lb\":%d,\"wt\":%d,\"gt\":%d,\"df\":%d,\"hp\":%d,\"lg\":%d}", 
      c.clockTimeoutS, c.sleepTimeoutS, (unsigned int)c.colorHoursConnected, (unsigned int)c.colorMinutesConnected, (unsigned int)c.colorSecondsConnected,
      (unsigned int)c.colorHoursDisc, (unsigned int)c.colorMinutesDisc, (unsigned int)c.colorSecondsDisc, c.brightnessPercent, c.lowBatteryThreshold,
      c.wakeThreshold, c.gyroThreshold, c.doubleFlickWindow, c.hapticPatternIndex, c.logarithmicBrightness ? 1 : 0);
    ble_handler.notifyConfig(json_buf);
  }

  // Setup BLE callbacks
  ble_handler.onHapticRX(onBLEHapticRX);
  ble_handler.onBearingUpdate(onBLEBearingUpdate);
  ble_handler.onDistanceUpdate(onBLEDistanceUpdate);
  ble_handler.onConfigUpdate(onBLEConfigUpdate);
  ble_handler.onCalibStart(onBLECalibStart);
  ble_handler.onCalibEnd(onBLECalibEnd);
  ble_handler.onCalibCancel(onBLECalibCancel);
  ble_handler.onCompassCalibStart(onBLECompassCalibStart);
  ble_handler.onThresholdWrite(onBLEThresholdWrite);
  ble_handler.onOTARequest(onBLEOTARequest);
  ble_handler.onTimeSync(onBLETimeSync); // NEW: real-time clock sync

  // State machine
  Serial.println("[SETUP] Initializing state machine...");
  state_machine.begin();

  // BUG-032: Register low-battery pulse handler so the 30s timer actually fires
  // a warning instead of just updating a timestamp with no effect.
  state_machine.setLowBatteryPulseCallback([]() {
    led_controller.errorBattery();
    haptic.playPattern(HAPTIC_PATTERN_BATTERY);
  });

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

  // CRITICAL: Reset the clock timer NOW so setup() overhead doesn't eat
  // into the display timeout. The timer was started during transitionTo()
  // above, but setup() itself can take several seconds. Without this, the
  // watch enters DEEP_SLEEP almost immediately on first boot.
  state_machine.resetTimer((uint32_t)runtime_config.getConfig().clockTimeoutS * 1000);

  // CRITICAL: Bluefruit.begin() re-enables the internal LED manager which
  // overrides our LED_BLUE = HIGH from above, causing the blue status LED
  // to blink continuously during BLE advertising. Disable it again here.
  Bluefruit.autoConnLed(false);
  digitalWrite(LED_BLUE, HIGH);

  Serial.println("[SETUP] Complete! Starting main loop...\n");

  loop_last_ms = millis();
}

// ============================================================================
// MAIN LOOP
// ============================================================================

void loop() {
  uint32_t now_ms = millis();

  noInterrupts();
  if (time_synced && (now_ms - millis_at_sync > 86400000UL)) {
    unix_base_ts += 86400; // Advance 1 day in seconds
    millis_at_sync += 86400000UL;
  }
  interrupts();

  // Maintain ~10ms loop interval
  if ((now_ms - loop_last_ms) < LOOP_INTERVAL_MS) {
    if (state_machine.getCurrentState() != STATE_DEEP_SLEEP) {
      return; // Not time yet
    }
  }
  loop_last_ms = now_ms;

  // Handle pending BLE commands safely outside of ISR
  if (ble_calib_start_pending) {
    ble_calib_start_pending = false;
    Serial.println("[BLE] Compass Calibration requested");
    if (state_machine.getCurrentState() == STATE_DEEP_SLEEP) {
      state_machine.transitionTo(STATE_WAKING_UP);
    }
    state_machine.transitionTo(STATE_COMPASS_CALIBRATION);
    compass.startCalibration();
  }
  if (ble_threshold_write_pending) {
    ble_threshold_write_pending = false;
    Serial.print("[BLE] Threshold write requested: ");
    Serial.println(ble_new_threshold);
    eeprom_manager.saveCalibration(ble_new_threshold);
    power_manager.updateIMUThreshold(ble_new_threshold);
    runtime_config.setWakeThreshold(ble_new_threshold);
  }

  // BUG-008: Snapshot the flag and JSON atomically before processing.
  // A rapid second BLE write could overwrite config_json_buf between the flag
  // clear and the getConfigJson() call, silently dropping the new config.
  if (config_update_pending) {
    char local_json[1024];
    noInterrupts();
    config_update_pending = false;
    strlcpy(local_json, ble_handler.getConfigJson(), sizeof(local_json));
    interrupts();
    if (local_json[0] != '\0') {
      runtime_config.updateFromJson(local_json);
      Serial.println("[CONFIG] Config parsed and applied to RAM");

      const RuntimeConfig &cfg = runtime_config.getConfig();
      static uint8_t last_applied_threshold = 0xFF;
      if (last_applied_threshold != cfg.wakeThreshold) {
        last_applied_threshold = cfg.wakeThreshold;
        power_manager.updateIMUThreshold(cfg.wakeThreshold);
        eeprom_manager.saveCalibration(cfg.wakeThreshold); // Sync the other direction
      }

      // BUG-041: Apply user-configurable timeouts to the active state.
      // clockTimeoutS / sleepTimeoutS were parsed from JSON and stored in RAM
      // but never fed back into the state machine — they had zero effect.
      {
        State cur = state_machine.getCurrentState();
        uint32_t clock_ms = (uint32_t)cfg.clockTimeoutS * 1000;
        uint32_t radar_ms = (uint32_t)cfg.sleepTimeoutS * 1000;
        if (cur == STATE_CLOCK_CONNECTED || cur == STATE_CLOCK_DISCONNECTED) {
          state_machine.resetTimer(clock_ms);
        } else if (cur == STATE_RADAR_MODE || cur == STATE_DISTANCE_MODE) {
          state_machine.resetTimer(radar_ms);
        }
      }

      // BUG-009: Use now_ms (captured at loop top) not a new millis() call.
      // If millis() rolls over between now_ms and this call, the debounce
      // timer gets a value far in the future, triggering an immediate save.
      last_config_change_ms = now_ms;
      config_save_pending = true;
    }
  }

  if (calib_cancel_pending) {
    calib_cancel_pending = false;
    imu_calibrator.cancel();
    if (state_machine.getCurrentState() != STATE_DEEP_SLEEP) {
      state_machine.transitionTo(state_machine.getPreviousState());
    }
  }

  if (calib_start_pending) {
    calib_start_pending = false;
    if (state_machine.getCurrentState() == STATE_DEEP_SLEEP) {
      state_machine.transitionTo(STATE_WAKING_UP);
    }
    state_machine.transitionTo(STATE_CALIBRATION_MODE);
  }

  if (ota_request_pending) {
    ota_request_pending = false;
    state_machine.transitionTo(STATE_OTA_MODE);
  }

  if (haptic_rx_pending) {
    if (state_machine.getCurrentState() != STATE_OTA_MODE) {
      if (state_machine.getCurrentState() == STATE_DEEP_SLEEP) {
        state_machine.transitionTo(STATE_WAKING_UP);
      } else if (state_machine.getCurrentState() != STATE_WAKING_UP) {
        haptic_rx_pending = false;
        state_machine.transitionTo(STATE_HAPTIC_RX);
      }
    } else {
      haptic_rx_pending = false;
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
  static State last_printed_state = state_machine.getCurrentState();
  State cur_state = state_machine.getCurrentState();
  if (cur_state != last_printed_state) {
    Serial.print("\n[STATE] Transition: ");
    Serial.print(state_machine.getStateName(last_printed_state));
    Serial.print(" -> ");
    Serial.println(state_machine.getStateName(cur_state));
    
    // Notify app if we just left RADAR_MODE (covers timeouts and gestures)
    if (last_printed_state == STATE_RADAR_MODE) {
      ble_handler.notifyRadarModeActive(false);
    }

    last_printed_state = cur_state;
  }

  // ========================================================================
  // SERIAL SIMULATOR
  // ========================================================================
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
    } else if (c == 'c' || c == 'C') {
      Serial.println("\n[SERIAL CMD] Triggering 3D Compass Calibration");
      Serial.println(">>> SPIN THE DEVICE IN A 3D FIGURE-8 FOR 10 SECONDS! <<<");
      compass.startCalibration();
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

  // ========================================================================
  // EARLY RETURN FOR DEEP SLEEP
  // ========================================================================
  if (cur_state == STATE_DEEP_SLEEP) {
    // Check Serial for simulated wakeups
    if (simulated_gesture == GESTURE_TAP_SIMPLE) {
        Serial.println("\n[SERIAL CMD] Simulating WAKEUP (TAP)");
        last_wake_source = WAKE_SOURCE_TAP;
        simulated_gesture = GESTURE_NONE; // Reset it so it doesn't trigger again
    }

#if IMU_WAKE_ENABLED
    // Process motion interrupt flag if it fired while asleep
    if (motion_detected_flag) {
      motion_detected_flag = false;
      Serial.println("[MOTION] Wake gesture detected (INT1 RISING)");
      if (ble_connected) {
        ble_handler.notifyCalibStatus(0xFF, 0xFF);
      }
    }
#endif

    // Run deep sleep logic to either sleep CPU or process wakeup
    handleStateDeepSleep(GESTURE_NONE);
    if (state_machine.getCurrentState() == STATE_DEEP_SLEEP) {
      state_machine.clearStateChanged();
      return;
    }
  }

  // ========================================================================
  // UPDATE PERIPHERALS
  // ========================================================================

  gesture_detector.setThreshold(runtime_config.getConfig().gyroThreshold);
  gesture_detector.setDoubleFlickWindow(
      runtime_config.getConfig().doubleFlickWindow);
  gesture_detector.update(now_ms);

  float ax = 0, ay = 0, az = 0;
  if (lsm6ds3_connected) {
    ax = lsm6ds3.readFloatAccelX();
    ay = lsm6ds3.readFloatAccelY();
    az = lsm6ds3.readFloatAccelZ();
  }
  compass.update(ax, ay, az);

  // ========================================================================
  // IMU STREAMING (10 Hz)
  // ========================================================================
  if (ble_handler.isIMUStreamRequested() && lsm6ds3_connected) {
    static uint32_t last_imu_stream_ms = 0;
    if (now_ms - last_imu_stream_ms >= 100) {
      last_imu_stream_ms = now_ms;
      float gx = lsm6ds3.readFloatGyroX();
      float gy = lsm6ds3.readFloatGyroY();
      float gz = lsm6ds3.readFloatGyroZ();
      
      float mg_f = sqrt(ax*ax + ay*ay + az*az) * 1000.0f;
      float dps_f = sqrt(gx*gx + gy*gy + gz*gz);
      
      uint16_t mg = (uint16_t)(mg_f > 65535.0f ? 65535 : mg_f);
      uint16_t dps = (uint16_t)(dps_f > 65535.0f ? 65535 : dps_f); // magnitude, always ≥ 0
      ble_handler.notifyIMUStream(mg, dps);
    }
  }

  // Imprimir lecturas de la brújula periódicamente por serial para diagnóstico
  // (Removed debug block to clear console spam)

  ble_handler.update();
  power_manager.update();

  // Enviar nivel de batería al móvil de forma global (independiente del estado)
  // y con flanco instantáneo en la conexión para evitar el lag de 10 segundos.
  static bool ble_was_connected = false;
  static uint32_t last_battery_notify_ms = 0;
  
  ble_connected = ble_handler.isConnected();
  battery_percent = power_manager.getBatteryPercent();
  
  if (ble_connected) {
    if (!ble_was_connected) {
      ble_was_connected = true;
      last_battery_notify_ms = now_ms;
      ble_handler.notifyBattery(battery_percent);
      
      // FW Bug 7 Fix: When BLE connects, if we are in deep sleep, we must 
      // explicitly transition to STATE_WAKING_UP so that the IMU and compass 
      // are properly powered on and initialized.
      if (state_machine.getCurrentState() == STATE_DEEP_SLEEP) {
        sleep_entry_done = false;
        state_machine.transitionTo(STATE_WAKING_UP);
      }
    } else if (now_ms - last_battery_notify_ms > 10000) {
      last_battery_notify_ms = now_ms;
      ble_handler.notifyBattery(battery_percent);
    }
  } else {
    ble_was_connected = false;
  }

  led_controller.update(now_ms);
  haptic.update(now_ms);

  // ========================================================================
  // READ INPUTS & SERIAL SIMULATOR
  // ========================================================================

  GestureType gesture = gesture_detector.getGesture();

  // Notify app of gyro flick detection for live calibration feedback (0xFE =
  // Gyro Flick)
  if (gesture != GESTURE_NONE && ble_connected) {
    ble_handler.notifyCalibStatus(0xFE, 0xFE);
  }

  if (simulated_gesture != GESTURE_NONE && state_machine.getCurrentState() != STATE_DEEP_SLEEP) {
    gesture = simulated_gesture;
    simulated_gesture = GESTURE_NONE; // Reset
  }

  // Update battery overlay
  if (power_manager.isLowBattery()) {
    state_machine.setLowBatteryActive(true);
  } else {
    state_machine.setLowBatteryActive(false);
  }

// Force deep sleep if critical battery (unless disabled for testing)
#if !DEBUG_DISABLE_DEEP_SLEEP
  if (power_manager.isCriticalBattery() && state_machine.getCurrentState() != STATE_DEEP_SLEEP && state_machine.getCurrentState() != STATE_BATTERY_DEAD_DISPLAY) {
    state_machine.transitionTo(STATE_BATTERY_DEAD_DISPLAY);
  }
#endif

  // ========================================================================
  // STATE MACHINE UPDATE
  // ========================================================================

#if IMU_WAKE_ENABLED
  // Manual calibration logging block removed
#endif

  static State last_handled_state = STATE_DEEP_SLEEP;
  if (last_handled_state == state_machine.getCurrentState()) {
    state_machine.clearStateChanged();
  }
  last_handled_state = state_machine.getCurrentState();

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

  case STATE_BATTERY_DEAD_DISPLAY:
    if (state_machine.stateChanged()) {
      led_controller.errorBattery();
    }
    // Timeout handled by state_machine (2s -> DEEP_SLEEP)
    break;

  case STATE_COMPASS_CALIBRATION:
    handleStateCompassCalibration();
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
  // BUG-010: Peripheral shutdown runs ONCE on state entry, not every 10ms tick.
  // Previously setPower(false) ran NeoPixel show() + delayMicroseconds(50) on
  // every loop iteration, burning ~460µs/tick (~5% CPU) while "sleeping".
  // Note: stateChanged() returns true on every tick in DEEP_SLEEP because
  // state_machine.update() is never called (early-return path). The idempotency
  // fix in setPower() and stopMotor() makes repeated calls free.
  // 
  // IMPORTANT: If adding new wake paths (e.g., BLE-triggered wake), ensure 
  // sleep_entry_done is reset to false when exiting this handler.
  // Additionally, because of the early-return pattern, state_machine.update() 
  // is skipped during DEEP_SLEEP, meaning timer and low_battery_pulse logic 
  // will not execute while sleeping.

  if (!sleep_entry_done) {
    sleep_entry_done = true;
    last_wake_source = WAKE_SOURCE_NONE;
    Serial.println("[POWER] Entering DEEP_SLEEP state. Configuring IMU for "
                   "Rise-to-Wake...");

    if (config_save_pending) {
      runtime_config.saveToFlash();
      config_save_pending = false;
      Serial.println("[CONFIG] Deferred config saved before sleep");
    }

    // Power off peripherals on entry
    led_controller.setPower(false); // idempotent after BUG-010 fix in setPower()
    haptic.stopMotor();             // idempotent after BUG-010 fix in stopMotor()

#if IMU_WAKE_ENABLED
    power_manager.setupIMUForRiseToWake();
#endif

    // Safety pull-downs for MOSFETs to prevent floating gate issues
    pinMode(PIN_MOTOR, OUTPUT);
    digitalWrite(PIN_MOTOR, LOW);
    pinMode(PIN_LED_POWER, OUTPUT);
    digitalWrite(PIN_LED_POWER, LOW);

    // Power down compass magnetometer to save power
    compass.powerDown();

    // Set low-power BLE advertising if not connected
    if (!ble_connected) {
      ble_handler.setLowPowerAdvertising(true);
    }
  }

  // Wait for button tap or motion to wake (via interrupts)
  int local_wake_source;
  noInterrupts();
  local_wake_source = last_wake_source;
  interrupts();

  if (local_wake_source != WAKE_SOURCE_NONE || (!ble_connected && ble_handler.isConnected())) {
    Serial.print("[POWER] Wake source detected: ");
    if (local_wake_source != WAKE_SOURCE_NONE) {
      Serial.println(local_wake_source == WAKE_SOURCE_MOTION ? "MOTION" : "TAP");
    } else {
      Serial.println("BLE CONNECTION");
    }

    // Clear wake source and wake up
    noInterrupts();
    last_wake_source = WAKE_SOURCE_NONE;
    interrupts();
    
    sleep_entry_done = false;
    state_machine.transitionTo(STATE_WAKING_UP);
    return;
  }

  // Periodic Low Battery Pulse in Deep Sleep
  static uint32_t last_low_batt_pulse_ms = 0;
  uint32_t wait_time = portMAX_DELAY;

  if (power_manager.isLowBattery()) {
    uint32_t time_since_pulse = millis() - last_low_batt_pulse_ms;
    if (time_since_pulse >= TIMER_LOW_BATTERY_PULSE_MS) {
      last_low_batt_pulse_ms = millis();
      led_controller.setPower(true);
      led_controller.errorBattery();
      delay(100);
      led_controller.setPower(false);
      time_since_pulse = 0;
    }
    wait_time = TIMER_LOW_BATTERY_PULSE_MS - time_since_pulse;
  }

  // Put CPU to sleep until next interrupt OR low battery pulse
  #if !DEBUG_DISABLE_DEEP_SLEEP
    power_manager.enterDeepSleep(wait_time);
  #else
    delay(10); // Don't hog CPU in debug stay-awake loop
  #endif
}

void handleStateWakingUp() {
  if (state_machine.stateChanged()) {
    sleep_entry_done = false;
    // BUG-026: Clear sleeping flag so isAsleep() reflects reality.
    power_manager.wakeFromSleep();

    // Power on LED ring
    led_controller.setPower(true);

    // Restore normal advertising interval if not connected
    if (!ble_handler.isConnected()) {
      ble_handler.setLowPowerAdvertising(false);
    }

    // Power on compass magnetometer
    compass.powerUp();

    // Clear any leftover gestures
    gesture_detector.reset();

    // Re-initialize LSM6DS3 for normal gesture detection
    if (lsm6ds3_connected) {
      Serial.println("[POWER] Waking up. Restoring IMU to normal mode...");
      lsm6ds3.begin();
      delay(20); // Let IMU settle before reading gestures
      
      // INTEGRATION-002: Discard first sample which is often garbage after wake
      lsm6ds3.readFloatAccelX();
      lsm6ds3.readFloatAccelY();
      lsm6ds3.readFloatAccelZ();
      lsm6ds3.readFloatGyroX();
      lsm6ds3.readFloatGyroY();
      lsm6ds3.readFloatGyroZ();
    }

    // Note: Bit-bang GPIO is immune to the mbed Wire1 TWI driver bug, so we no
    // longer need to re-initialize the compass here.
  }

  // If a haptic event woke the watch or arrived while sleeping, process it
  // first
  if (haptic_rx_pending) {
    haptic_rx_pending = false; // Reset flag
    state_machine.transitionTo(STATE_HAPTIC_RX);
  } else {
    // Check BLE connection and transition accordingly
    if (ble_handler.isConnected()) {
      state_machine.transitionTo(STATE_CLOCK_CONNECTED);
    } else {
      state_machine.transitionTo(STATE_CLOCK_DISCONNECTED);
    }
  }
}

void handleStateClockConnected(GestureType gesture, uint32_t now_ms) {
  static uint32_t last_time_update_ms = 0;

  // Update clock display every 33ms (~30 Hz) to reduce power sags and BLE
  // collisions
  if ((now_ms - last_time_update_ms) >= 33) {
    last_time_update_ms = now_ms;

    uint64_t current_unix_ms = getRealEpochMs();

    int32_t local_tz;
    bool local_synced;
    noInterrupts();
    local_tz = tz_offset_s;
    local_synced = time_synced;
    interrupts();

    uint32_t base_secs = (uint32_t)(current_unix_ms / 1000ULL);
    uint32_t total_secs;

    if (local_synced && base_secs > 1000000000UL) {
      total_secs = base_secs + local_tz;
    } else {
      total_secs = base_secs;
    }
    
    uint8_t seconds = total_secs % 60;
    uint8_t minutes = (total_secs / 60) % 60;
    uint8_t hours = (total_secs / 3600) % 24;
    uint16_t millis_part = (uint16_t)(current_unix_ms % 1000ULL);

    led_controller.updateClockTime(hours, minutes, seconds, millis_part);
    led_controller.showClock(true); // connected = white colors
  }

  // Handle gestures
  switch (gesture) {
  case GESTURE_TAP_SIMPLE:
    // Transition to RADAR_MODE (from clock)
    state_machine.transitionTo(STATE_RADAR_MODE);
    break;

  case GESTURE_TAP_DOUBLE:
    // Ignored in clock mode (reset timer to stay awake)
    state_machine.resetTimer((uint32_t)runtime_config.getConfig().clockTimeoutS * 1000);
    break;

  case GESTURE_TAP_TRIPLE:
    // Send haptic to pareja (3 flicks)
    state_machine.transitionTo(STATE_HAPTIC_TX);
    break;

  case GESTURE_PRESS_SHORT:
    // Ignored (button disabled)
    break;

  case GESTURE_PRESS_LONG:
    // Force deep sleep (via button if re-enabled)
    state_machine.transitionTo(STATE_DEEP_SLEEP);
    break;

  default:
    break;
  }

  // Check BLE disconnection
  if (!ble_handler.isConnected()) {
    state_machine.transitionTo(STATE_CLOCK_DISCONNECTED);
  }
}

void handleStateClockDisconnected(GestureType gesture, uint32_t now_ms) {
  static uint32_t last_time_update_ms = 0;

  // Update clock display every 500ms (~2 Hz) to give BLE advertising plenty of
  // idle time
  if ((now_ms - last_time_update_ms) >= 500) {
    last_time_update_ms = now_ms;

    uint64_t current_unix_ms = getRealEpochMs();

    int32_t local_tz;
    bool local_synced;
    noInterrupts();
    local_tz = tz_offset_s;
    local_synced = time_synced;
    interrupts();

    uint32_t base_secs = (uint32_t)(current_unix_ms / 1000ULL);
    uint32_t total_secs;

    if (local_synced && base_secs > 1000000000UL) {
      total_secs = base_secs + local_tz;
    } else {
      total_secs = base_secs;
    }
    
    uint8_t seconds = total_secs % 60;
    uint8_t minutes = (total_secs / 60) % 60;
    uint8_t hours = (total_secs / 3600) % 24;
    uint16_t millis_part = (uint16_t)(current_unix_ms % 1000ULL);

    led_controller.updateClockTime(hours, minutes, seconds, millis_part);
    led_controller.showClock(false); // disconnected = blue colors
  }

  // Handle gestures (limited functionality without BLE)
  switch (gesture) {
  case GESTURE_TAP_SIMPLE:
    state_machine.resetTimer((uint32_t)runtime_config.getConfig().clockTimeoutS * 1000);
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
    // Toggle back to CLOCK
    state_machine.transitionTo(ble_handler.isConnected() ? STATE_CLOCK_CONNECTED : STATE_CLOCK_DISCONNECTED);
    break;

  case GESTURE_TAP_DOUBLE:
    // Toggle to DISTANCE_MODE
    state_machine.transitionTo(STATE_DISTANCE_MODE);
    break;

  case GESTURE_TAP_TRIPLE:
    // Send haptic to pareja (3 flicks)
    state_machine.transitionTo(STATE_HAPTIC_TX);
    break;

  case GESTURE_PRESS_SHORT:
    // Ignored (button disabled)
    break;

  case GESTURE_PRESS_LONG:
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
    // Toggle back to CLOCK
    state_machine.transitionTo(ble_handler.isConnected() ? STATE_CLOCK_CONNECTED : STATE_CLOCK_DISCONNECTED);
    break;

  case GESTURE_TAP_DOUBLE:
    // Toggle to RADAR_MODE
    state_machine.transitionTo(STATE_RADAR_MODE);
    break;

  case GESTURE_TAP_TRIPLE:
    // Send haptic to pareja (3 flicks)
    state_machine.transitionTo(STATE_HAPTIC_TX);
    break;

  case GESTURE_PRESS_SHORT:
    // Ignored (button disabled)
    break;

  case GESTURE_PRESS_LONG:
    state_machine.transitionTo(STATE_DEEP_SLEEP);
    break;

  default:
    break;
  }
}

void handleStateHapticTX() {
  static uint8_t pulse_step = 0;
  static uint32_t step_timer = 0;

  if (state_machine.stateChanged()) {
    pulse_step = 0;
    step_timer = millis();
    ble_handler.notifyHapticTX();
  }

  if (runtime_config.getConfig().hapticPatternIndex == 1) { // 1 = only partner
    return;
  }

  uint32_t now = millis();
  uint8_t brightness_pct = runtime_config.getConfig().brightnessPercent;
  uint8_t base_brightness = (brightness_pct * 255) / 100;

  switch (pulse_step) {
    case 0:
      led_controller.fillWithBrightness(COLOR_HAPTIC_RX, base_brightness);
      step_timer = now;
      pulse_step = 1;
      break;
    case 1:
      if (now - step_timer >= 100) {
        led_controller.clear();
        led_controller.show();
        step_timer = now;
        pulse_step = 2;
      }
      break;
    case 2:
      if (now - step_timer >= 100) {
        led_controller.fillWithBrightness(COLOR_HAPTIC_RX, base_brightness);
        step_timer = now;
        pulse_step = 3;
      }
      break;
    case 3:
      if (now - step_timer >= 100) {
        led_controller.clear();
        led_controller.show();
        pulse_step = 4;
      }
      break;
    case 4:
      // Pattern complete. State machine timeout will handle the exit.
      break;
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
    state_machine.resetTimer(haptic.getPatternLength(HAPTIC_PATTERN_RX) + 500);
  }

  // Tap to cancel
  if (gesture == GESTURE_TAP_SIMPLE) {
    haptic.stopPattern();
    pattern_started = false;
    state_machine.transitionTo(ble_handler.isConnected() ? STATE_CLOCK_CONNECTED : STATE_CLOCK_DISCONNECTED);
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
  static bool success_shown = false;

  if (state_machine.stateChanged()) {
    calib_started = false;
    last_progress = 0xFF;
    success_shown = false;
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
    if (!success_shown) {
      uint8_t threshold = imu_calibrator.getThreshold();

      // Save to flash
      eeprom_manager.saveCalibration(threshold);

      // Update IMU with new threshold
      power_manager.updateIMUThreshold(threshold);

      char json_buf[32];
      snprintf(json_buf, sizeof(json_buf), "{\"wt\":%d}", threshold);
      runtime_config.updateFromJson(json_buf);

      // Notify app
      ble_handler.notifyCalibThreshold(threshold);

      // Flash green success
      led_controller.fillWithBrightness(COLOR_SUCCESS, base_brightness);
      haptic.playPattern(HAPTIC_PATTERN_TX);
      
      success_shown = true;
      state_machine.resetTimer(1000);
    } else if (state_machine.isTimerExpired()) {
      calib_started = false;
      state_machine.transitionTo(ble_handler.isConnected() ? STATE_CLOCK_CONNECTED : STATE_CLOCK_DISCONNECTED);
    }
  }

  // Tap to cancel
  if (gesture == GESTURE_TAP_SIMPLE) {
    imu_calibrator.cancel();
    calib_started = false;
    state_machine.transitionTo(ble_handler.isConnected() ? STATE_CLOCK_CONNECTED : STATE_CLOCK_DISCONNECTED);
  }
}

void handleStateOTAMode() {
  // BUG-007: Reset static locals on state entry.
  // ota_enter_time is a static local that was never reset. On a second OTA
  // request after a cancellation, it still held the old millis() value,
  // so (millis() - ota_enter_time) was immediately > 1500ms, causing an
  // instant silent reboot with no LED/haptic confirmation.
  static uint8_t ota_progress = 0;
  static uint32_t ota_enter_time = 0;
  static bool ota_timer_started = false;

  if (state_machine.stateChanged()) {
    ota_timer_started = false;
    ota_enter_time = 0;
    ota_progress = 0;
  }

  led_controller.updateOTAProgress(ota_progress);

  // Wait 1.5s for the user to see LEDs and feel haptic, then reboot into DFU
  if (!ota_timer_started) {
    ota_enter_time = millis();
    ota_timer_started = true;
    haptic.playPattern(HAPTIC_PATTERN_TX); // Buzz to confirm
  }

  if (ota_timer_started && (millis() - ota_enter_time) > 1500) {
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
  haptic_rx_pending = true;
}

// BUG-003: float and uint32_t writes are single instructions on Cortex-M4 when
// aligned, but noInterrupts() guards future-proof against compiler reordering
// and ensure correctness if these types ever change.
void onBLEBearingUpdate(float bearing) {
  noInterrupts();
  current_bearing = bearing;
  interrupts();
}

void onBLEDistanceUpdate(uint32_t distance_m) {
  noInterrupts();
  current_distance = distance_m;
  interrupts();
}

void onBLECalibStart() {
  // App requested start calibration
  calib_start_pending = true;
}

void onBLECalibEnd() {
  // App requested end calibration (might be premature)
  // Calibrator will decide to finalize or continue
  // This is mainly for UI: app button "Done"
}

void onBLECalibCancel() {
  // BUG-006: Do NOT touch state machine or calibrator here.
  // This runs in BLE ISR context. state_machine.transitionTo() writes three
  // non-atomic variables (current_state, previous_state, state_changed).
  // Defer to loop() via flag where it is safe to run.
  calib_cancel_pending = true;
}

void onBLECompassCalibStart() {
  // ISR context: Flag for main loop execution
  ble_calib_start_pending = true;
}

void onBLEThresholdWrite(uint8_t threshold) {
  // ISR context: Flag for main loop execution
  ble_new_threshold = threshold;
  ble_threshold_write_pending = true;
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
  ota_request_pending = true;
}

void onBLETimeSync(uint32_t unix_ts, int32_t tz_offset) {
  // BUG-003: All variables must be written atomically.
  // The main loop reads time_synced + unix_base_ts + millis_at_sync together.
  // Without the guard, loop() can see time_synced=true but stale millis_at_sync,
  // computing a wrong epoch for one tick.
  noInterrupts();
  unix_base_ts   = unix_ts;
  tz_offset_s    = tz_offset;
  millis_at_sync = millis();
  time_synced    = true;
  interrupts();

  // Log human-readable time (UTC) — outside critical section, Serial is slow
  uint32_t local_secs = unix_ts + tz_offset;
  uint8_t local_hrs = (local_secs / 3600) % 24;
  uint8_t local_mins = (local_secs / 60) % 60;
  uint8_t local_secs_remainder = local_secs % 60;
  Serial.print("[TIME] Clock synced — Local Time: ");
  if (local_hrs  < 10) Serial.print('0');
  Serial.print(local_hrs);
  Serial.print(':');
  if (local_mins < 10) Serial.print('0');
  Serial.print(local_mins);
  Serial.print(':');
  if (local_secs_remainder < 10) Serial.print('0');
  Serial.print(local_secs_remainder);
  Serial.print(" (TZ offset: ");
  Serial.print(tz_offset);
  Serial.println("s)");
}

// ============================================================================
// INTERRUPT HANDLERS
// ============================================================================

void wakeMainLoop() {
  if (power_manager.wake_sem) {
    xSemaphoreGive(power_manager.wake_sem);
  }
}

void onMotionWakeup() {
  // NEW: Interrupt from IMU (INT1) during deep sleep (rise-to-wake)
  last_wake_source = WAKE_SOURCE_MOTION;
  motion_detected_flag = true;
  
  if (power_manager.wake_sem) {
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    xSemaphoreGiveFromISR(power_manager.wake_sem, &xHigherPriorityTaskWoken);
    portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
  }
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
  GestureType last_gesture = gesture_detector.getDetectedGesture();
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

void handleStateCompassCalibration() {
  if (!compass.isCalibrating()) {
    state_machine.transitionTo(ble_handler.isConnected() ? STATE_CLOCK_CONNECTED : STATE_CLOCK_DISCONNECTED);
    return;
  }
  // Chasing LED animation logic
  led_controller.clear();
  led_controller.setLEDBrightness((millis() / 100) % LED_COUNT, COLOR_INFO, 255);
  led_controller.show();
}

