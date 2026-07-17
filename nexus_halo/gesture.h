#ifndef GESTURE_H
#define GESTURE_H

#include "config.h"
#include <Arduino.h>
#include <LSM6DS3.h>
#undef Wire
#include <cmath>

// ============================================================================
// GESTURE DETECTION (LSM6DS3 Gyroscope / Accelerometer)
// ============================================================================
// Detects wrist flicks/twists using the gyroscope to replace capacitive button

class GestureDetector {
public:
  GestureDetector();
  
  // Initialize
  void begin();
  
  // Call frequently from loop (~10ms)
  void update(uint32_t now_ms);
  
  // Get detected gesture (clears after reading)
  GestureType getGesture();
  
  // Non-destructive read for debug prints
  GestureType getDetectedGesture() const { return detected_gesture; }
  
  // Clear flick tracking states
  void reset();
  
  // Set reference to the LSM6DS3 sensor
  void setIMU(LSM6DS3* sensor) { imu_ptr = sensor; }

  // Set gyro threshold dynamically
  void setThreshold(uint16_t ths) { gyro_threshold = ths; }
  
  // Set double-flick timing window dynamically
  void setDoubleFlickWindow(uint16_t ms) { double_flick_window = ms; }
  

private:
  // Gyro flick detection (NEW)
  LSM6DS3* imu_ptr;
  uint32_t last_flick_ms;
  uint32_t sequence_start_ms;
  uint8_t flick_count;
  bool flick_reset;
  uint16_t gyro_threshold;
  uint16_t double_flick_window;
  uint8_t tear_debounce;
  
  // Gesture result
  GestureType detected_gesture;
  bool gesture_reported;
  
};

#endif // GESTURE_H
