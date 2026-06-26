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
  
  // Clear flick tracking states
  void reset();
  
  // Set reference to the LSM6DS3 sensor
  void setIMU(LSM6DS3* sensor) { imu_ptr = sensor; }

  // Set gyro threshold dynamically
  void setThreshold(uint16_t ths) { gyro_threshold = ths; }
  
  // Set double-flick timing window dynamically
  void setDoubleFlickWindow(uint16_t ms) { double_flick_window = ms; }
  
  // Raw button state (kept for backward compatibility)
  bool isButtonPressed() const { return button_pressed; }
  
  // Debug info
  uint32_t getButtonPressDuration() const;

private:
  // Pin and state (legacy)
  int pin_button;
  bool button_pressed;
  bool button_pressed_last;
  
  // Debounce (legacy)
  uint32_t last_edge_ms;
  
  // Tap detection (legacy)
  uint32_t tap_start_ms;
  uint32_t tap_end_ms;
  bool tap_in_progress;
  
  // Double-tap detection (legacy)
  uint32_t first_tap_end_ms;
  bool waiting_for_second_tap;
  
  // Gyro flick detection (NEW)
  LSM6DS3* imu_ptr;
  uint32_t last_flick_ms;
  uint32_t first_flick_ms;
  bool waiting_for_second_flick;
  bool flick_reset;
  uint16_t gyro_threshold;
  uint16_t double_flick_window;
  
  // Gesture result
  GestureType detected_gesture;
  bool gesture_reported;
  
  // Helper methods
  void _onButtonDown(uint32_t now_ms);
  void _onButtonUp(uint32_t now_ms);
};

#endif // GESTURE_H
