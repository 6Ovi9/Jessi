#ifndef POWER_H
#define POWER_H

#include "config.h"
#include <Arduino.h>
#include <FreeRTOS.h>
#include <semphr.h>

// ============================================================================
// POWER MANAGEMENT (Deep Sleep, Wake, Battery)
// ============================================================================

class PowerManager {
public:
  PowerManager();
  
  SemaphoreHandle_t wake_sem;
  
  // Initialize power management and battery monitoring
  void begin();
  
  // Get battery level (0-100%)
  uint8_t getBatteryPercent() const { return battery_percent; }
  
  // Update battery reading (call from loop)
  void update();
  
  // Check if battery is low or critical
  bool isLowBattery() const {
#if DEBUG_BYPASS_BATTERY_CHECK
    return false;
#else
    return battery_percent < LOW_BATTERY_THRESHOLD_PERCENT;
#endif
  }
  bool isCriticalBattery() const {
#if DEBUG_BYPASS_BATTERY_CHECK
    return false;
#else
    return battery_percent < CRITICAL_BATTERY_PERCENT;
#endif
  }
  
  // Enter deep sleep mode
  void enterDeepSleep(uint32_t timeout_ms = portMAX_DELAY);
  
  // Wake from deep sleep (call after interrupt)
  void wakeFromSleep();
  
  // Power-down internal peripherals (IMU if not using rise-to-wake, microphone)
  // In v1.2: IMU stays in low-power mode for rise-to-wake, only PDM is powered down
  void powerDownInternalSensors();
  
  // Configure IMU for low-power wake-on-motion (rise-to-wake)
  void setupIMUForRiseToWake();
  
  // Update IMU threshold from calibration
  void updateIMUThreshold(uint8_t new_threshold);
  
  // Get current power state
  bool isAsleep() const { return sleeping; }

private:
  uint8_t battery_percent;
  uint32_t last_battery_update_ms;
  bool sleeping;
  
  // ADC pin for battery voltage (if available)
  int pin_battery_adc;
  uint8_t wake_threshold;
  
  // Helper methods
  uint8_t _readBatteryVoltage();
  void _setupBatteryADC();
};

#endif // POWER_H
