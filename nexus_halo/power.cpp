#include "power.h"
#include <Wire.h>
#include <LSM6DS3.h>  // Seeed official library
#undef Wire           // Prevent LSM6DS3.h from redefining Wire to Wire1 in our code
#include <PDM.h>
#include <nrf_power.h>
#include <nrf_gpio.h>

#ifdef SOFTDEVICE_PRESENT
#include "nrf_soc.h"
#include "nrf_sdm.h"
#endif

#ifndef PIN_VBAT_ENABLE
#define PIN_VBAT_ENABLE VBAT_ENABLE
#endif

#define LSM6DS3_REG_WAKE_UP_THS  0x5B
#define LSM6DS3_REG_WAKE_UP_DUR  0x5C
#define LSM6DS3_REG_MD1_CFG      0x5E

static void _writeLSM6DS3Register(uint8_t reg, uint8_t value) {
  // Safe I2C write on Wire1 - LSM6DS3 is on internal pins
  Wire1.beginTransmission(0x6A); // LSM6DS3 I2C address is 0x6A
  Wire1.write(reg);
  Wire1.write(value);
  Wire1.endTransmission(true); // Stop condition
}

static bool _readLSM6DS3Register(uint8_t reg, uint8_t &out) {
  Wire1.beginTransmission(0x6A);
  Wire1.write(reg);
  if (Wire1.endTransmission(true) != 0) return false;
  Wire1.requestFrom((uint8_t)0x6A, (uint8_t)1);
  if (!Wire1.available()) return false;
  out = Wire1.read();
  return true;
}

PowerManager::PowerManager()
  : battery_percent(100),
    last_battery_update_ms(0),
    sleeping(false),
#ifdef PIN_VBAT
    pin_battery_adc(PIN_VBAT),
#else
    pin_battery_adc(A6),
#endif
    wake_threshold(IMU_WAKE_UP_THS_DEFAULT)
{
}

void PowerManager::begin() {
  wake_sem = xSemaphoreCreateBinary();
  
  _setupBatteryADC();
  
  // Don't power down here; that happens in nexus_halo.ino setup()
  // because we need to do it before initializing other peripherals
}

void PowerManager::update() {
  uint32_t now_ms = millis();
  
  // Update battery every 5 seconds
  if ((now_ms - last_battery_update_ms) > 5000) {
    last_battery_update_ms = now_ms;
    battery_percent = _readBatteryVoltage();
  }
}

void PowerManager::powerDownInternalSensors() {
  // ============================================================================
  // v1.2: LSM6DS3 — Configured for Low-Power + Wake-on-Motion (NOT powered down)
  // ============================================================================
  
  #if !IMU_WAKE_ENABLED
    // BUG-023: Old code powered off then immediately back on. IMU was never truly off.
    // Correct: disable registers via I2C, then cut VDD permanently.
    Wire1.begin();
    Wire1.beginTransmission(0x6A); Wire1.write(0x11); Wire1.write(0x00); Wire1.endTransmission(); // Gyro off
    Wire1.beginTransmission(0x6A); Wire1.write(0x10); Wire1.write(0x00); Wire1.endTransmission(); // Accel off
    delay(5); // Register write settle - acceptable: this is setup path only, not loop()
    pinMode(PIN_LSM6DS3TR_C_POWER, OUTPUT);
    digitalWrite(PIN_LSM6DS3TR_C_POWER, LOW); // VDD stays low permanently
  #endif

  // ============================================================================
  // CRITICAL: Power-down PDM Microphone (integrated in XIAO nRF52840 Sense)
  // ============================================================================
  
  PDM.begin(1, 16000);
  PDM.end();
  // Manually disable PDM peripheral to shut down HFCLK and EasyDMA
  NRF_PDM->ENABLE = 0;
  
  // FIX: Force PDM clock and data pins back to disconnected inputs
  // The XIAO nRF52840 Sense uses P0.16 for PDM CLK and P1.00 for PDM DIN.
  NRF_P1->PIN_CNF[0] = (GPIO_PIN_CNF_DIR_Input << GPIO_PIN_CNF_DIR_Pos) |
                       (GPIO_PIN_CNF_INPUT_Disconnect << GPIO_PIN_CNF_INPUT_Pos);
  NRF_P0->PIN_CNF[16] = (GPIO_PIN_CNF_DIR_Input << GPIO_PIN_CNF_DIR_Pos) |
                        (GPIO_PIN_CNF_INPUT_Disconnect << GPIO_PIN_CNF_INPUT_Pos);
  
  // Serial.println("[POWER] PDM microphone powered down");
}

void PowerManager::setupIMUForRiseToWake() {
  Serial.println("[POWER] setupIMUForRiseToWake started");

  // Ensure IMU is powered
  pinMode(PIN_LSM6DS3TR_C_POWER, OUTPUT);
  digitalWrite(PIN_LSM6DS3TR_C_POWER, HIGH);
  delay(10);

  // Internal LSM6DS3TR-C on XIAO nRF52840 Sense uses Wire1.
  // Wire is the secondary I2C bus for external devices (D4/D5).
  Serial.println("[POWER] Initializing Wire1 (internal IMU)...");
  Wire1.begin();
  delay(10);

  Serial.println("[POWER] Resetting IMU registers via software reset...");
  _writeLSM6DS3Register(0x12, 0x01); // CTRL3_C Software reset
  delay(20); // Wait for reset to complete

  Serial.println("[POWER] Configuring IMU registers for wake-on-motion...");

  // 1. Disable Gyroscope to save power (CTRL2_G = 0x00 → off)
  _writeLSM6DS3Register(0x11, 0x00);

  // 2. Enable Accelerometer in low-power mode at 12.5 Hz (CTRL1_XL = 0x10)
  //    CTRL1_XL: ODR_XL=0001 (12.5Hz), FS_XL=00 (±2g), BW_XL=00
  _writeLSM6DS3Register(0x10, 0x10);

  // 3. Enable low-power mode (CTRL6_C bit LPM = 1)
  _writeLSM6DS3Register(0x15, 0x10);

  // 4. Set wake-up threshold — only bits[5:0] are used by the hardware.
  //    1 LSB = 62.5 mg at ±2G.  Mask to 6 bits to avoid writing garbage.
  _writeLSM6DS3Register(LSM6DS3_REG_WAKE_UP_THS, wake_threshold & 0x3F);
  Serial.print("[POWER] Wake threshold: 0x");
  Serial.print(wake_threshold & 0x3F, HEX);
  Serial.print(" = ");
  Serial.print((wake_threshold & 0x3F) * 62.5f, 0);
  Serial.println(" mg");

  // 5. Set wake-up duration (bits[7:6] of WAKE_UP_DUR).
  //    Each count = 1/ODR_XL = 80 ms at 12.5 Hz.
  //    Also clear SLEEP_DUR and TIMER_HR bits → write full register = 0xXX.
  _writeLSM6DS3Register(LSM6DS3_REG_WAKE_UP_DUR, (IMU_WAKE_UP_DUR & 0x03) << 5);
  Serial.print("[POWER] Wake duration: ");
  Serial.print(IMU_WAKE_UP_DUR + 1);
  Serial.print(" sample(s) = ~");
  Serial.print((IMU_WAKE_UP_DUR + 1) * 80);
  Serial.println(" ms");

  // 6. Route wake-up interrupt to INT1 pin (MD1_CFG: INT1_WU = bit5)
  _writeLSM6DS3Register(LSM6DS3_REG_MD1_CFG, 0x20);

  // 7. Enable basic interrupts globally (TAP_CFG (0x58) bit INTERRUPTS_ENABLE = 1)
  _writeLSM6DS3Register(0x58, 0x80);



  Serial.println("[POWER] IMU configured for rise-to-wake via INT1.");
  Serial.println("[POWER] setupIMUForRiseToWake finished");
}

void PowerManager::updateIMUThreshold(uint8_t new_threshold) {
  wake_threshold = new_threshold;
  uint8_t current_val = 0;
  uint8_t masked_val;
  if (_readLSM6DS3Register(LSM6DS3_REG_WAKE_UP_THS, current_val)) {
    masked_val = (current_val & 0xC0) | (new_threshold & 0x3F);
  } else {
    masked_val = new_threshold & 0x3F;
    Serial.println("[POWER] Warning: could not read WAKE_UP_THS - upper bits cleared");
  }
  _writeLSM6DS3Register(LSM6DS3_REG_WAKE_UP_THS, masked_val);
  Serial.print("[POWER] IMU wake threshold updated: 0x");
  Serial.println(masked_val, HEX);
}

void PowerManager::enterDeepSleep(uint32_t timeout_ms) {
  sleeping = true;
  
  // Power down external components via GPIO
  // (handled by state machine / led_controller / haptic_controller)
  
  // Configure interrupt for button wake-up (D8)
  // (handled elsewhere)
  
  if (wake_sem) {
    // Let FreeRTOS handle tickless idle sleep. Wakes up on timeout OR interrupt.
    xSemaphoreTake(wake_sem, timeout_ms == portMAX_DELAY ? portMAX_DELAY : pdMS_TO_TICKS(timeout_ms));
  }
}

void PowerManager::wakeFromSleep() {
  sleeping = false;
  // Resume normal operation
  // (handled by main state machine)
}

uint8_t PowerManager::_readBatteryVoltage() {
#if DEBUG_BYPASS_BATTERY_CHECK
  return 100;
#else
  // Enable voltage divider
  #ifdef PIN_VBAT_ENABLE
  digitalWrite(PIN_VBAT_ENABLE, LOW);
  delay(1); // Wait for voltage to settle
  #endif

  analogReference(AR_INTERNAL);  // BUG-025: 0.6V ref, 1/6 gain = 3.6V full-scale at 12-bit
  analogReadResolution(12);
  int raw = analogRead(pin_battery_adc);
  
  #ifdef PIN_VBAT_ENABLE
  pinMode(PIN_VBAT_ENABLE, INPUT);     // Disable voltage divider safely (High-Z)
  #endif
  
  // XIAO: 12-bit ADC (0-4095), reference 3.6V (internal bandgap)
  float voltage = (raw / 4095.0f) * 3.6f;
  
  // With 1M / 510K voltage divider:
  // V_bat = V_adc * (1510 / 510) ≈ V_adc * 2.96078
  float battery_voltage = voltage * 2.96078f;
  
  // LiPo typical voltage range: 3.0V (0%) to 4.2V (100%)
  float battery_percent_calc = ((battery_voltage - 3.0f) / (4.2f - 3.0f)) * 100.0f;
  
  // Clamp to 0-100%
  if (battery_percent_calc < 0) battery_percent_calc = 0;
  if (battery_percent_calc > 100) battery_percent_calc = 100;
  
  return (uint8_t)battery_percent_calc;
#endif
}

void PowerManager::_setupBatteryADC() {
#if !DEBUG_BYPASS_BATTERY_CHECK
  // Configure ADC for battery monitoring
  pinMode(pin_battery_adc, INPUT);
  analogReadResolution(12);  // 12-bit resolution for XIAO
  
  #ifdef PIN_VBAT_ENABLE
  pinMode(PIN_VBAT_ENABLE, INPUT); // High-Z disables voltage divider safely
  #endif
#endif
}
