#include "power.h"
#include <Adafruit_LSM6DS3TRC.h>
#include <PDM.h>
#include <nrf_power.h>
#include <nrf_gpio.h>

PowerManager::PowerManager()
  : battery_percent(100),
    last_battery_update_ms(0),
    sleeping(false),
    pin_battery_adc(A0)  // Adjust based on XIAO pin mapping
{
}

void PowerManager::begin() {
  _setupBatteryADC();
  
  // Don't power down here; that happens in couples_watch.ino setup()
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
  
  #if IMU_WAKE_ENABLED
    setupIMUForRiseToWake();
    // Serial.println("[POWER] IMU configured for rise-to-wake (not powered down)");
  #else
    // If rise-to-wake is disabled, power down IMU completely
    Adafruit_LSM6DS3TRC imu;
    Wire.setPins(D4, D5);
    Wire.begin();
    if (imu.begin_I2C()) {
      imu.setAccelDataRate(LSM6DS_RATE_SHUTDOWN);
      imu.setGyroDataRate(LSM6DS_RATE_SHUTDOWN);
    }
  #endif

  // ============================================================================
  // CRITICAL: Power-down PDM Microphone (integrated in XIAO nRF52840 Sense)
  // ============================================================================
  
  PDM.begin(1, 16000);
  PDM.end();
  
  // Serial.println("[POWER] PDM microphone powered down");
}

void PowerManager::setupIMUForRiseToWake() {
  // ============================================================================
  // Configure LSM6DS3 in Low-Power mode with Wake-on-Motion detection
  // ============================================================================
  
  Adafruit_LSM6DS3TRC imu;
  
  Wire.setPins(D4, D5);
  Wire.begin();
  
  if (!imu.begin_I2C()) {
    // Serial.println("[POWER] Failed to initialize LSM6DS3 for rise-to-wake");
    return;
  }
  
  // Disable gyroscope (not needed for wake-on-motion, saves power)
  imu.setGyroDataRate(LSM6DS_RATE_SHUTDOWN);
  
  // Configure accelerometer for low-power mode
  // 26 Hz is the minimum rate that supports wake-on-motion
  imu.setAccelDataRate(LSM6DS_RATE_26_HZ);
  imu.setAccelRange(LSM6DS_ACCEL_RANGE_2_G);
  
  // Configure Wake-on-Motion via registers
  imu.writeRegister(LSM6DS_WAKE_UP_THS, IMU_WAKE_UP_THS_DEFAULT);
  imu.writeRegister(LSM6DS_WAKE_UP_DUR, IMU_WAKE_UP_DUR);
  imu.writeRegister(LSM6DS_MD1_CFG, 0x20);
  
  // Serial.println("[POWER] LSM6DS3 configured for low-power wake-on-motion");
}

void PowerManager::updateIMUThreshold(uint8_t new_threshold) {
  Adafruit_LSM6DS3TRC imu;
  
  Wire.setPins(D4, D5);
  Wire.begin();
  
  if (imu.begin_I2C()) {
    imu.writeRegister(LSM6DS_WAKE_UP_THS, new_threshold);
  }
}

void PowerManager::enterDeepSleep() {
  sleeping = true;
  
  // Power down external components via GPIO
  // (handled by state machine / led_controller / haptic_controller)
  
  // Configure interrupt for button wake-up (D8)
  // (handled elsewhere)
  
  // Enter deep sleep using nRF52840 power control
  // The BLE stack (SoftDevice) must remain active for advertising
  
  // Wait for interrupt (this is a simplified version)
  // In production, use: sd_app_evt_wait() from SoftDevice
  // Or use __WFE() (wait for event) if not using SoftDevice
  
  // For now, just sleep the CPU and let interrupts wake it
  __WFI();  // Wait for interrupt
}

void PowerManager::wakeFromSleep() {
  sleeping = false;
  // Resume normal operation
  // (handled by main state machine)
}

uint8_t PowerManager::_readBatteryVoltage() {
  // Read ADC pin connected to battery voltage divider
  // Convert to percentage based on LiPo voltage curve
  
  int raw = analogRead(pin_battery_adc);
  
  // XIAO: 12-bit ADC (0-4095), reference 3.3V
  float voltage = (raw / 4095.0f) * 3.3f;
  
  // LiPo typical voltage range: 3.0V (0%) to 4.2V (100%)
  // Account for any voltage divider scaling if applicable
  
  float battery_percent_calc = ((voltage - 3.0f) / (4.2f - 3.0f)) * 100.0f;
  
  // Clamp to 0-100%
  if (battery_percent_calc < 0) battery_percent_calc = 0;
  if (battery_percent_calc > 100) battery_percent_calc = 100;
  
  return (uint8_t)battery_percent_calc;
}

void PowerManager::_setupBatteryADC() {
  // Configure ADC for battery monitoring
  // XIAO pins can be used for analog input
  // Default: use the pin defined in power.h
  
  pinMode(pin_battery_adc, INPUT);
  analogReadResolution(12);  // 12-bit resolution for XIAO
}
