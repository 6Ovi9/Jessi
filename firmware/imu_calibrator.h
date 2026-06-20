#ifndef IMU_CALIBRATOR_H
#define IMU_CALIBRATOR_H

#include "config.h"
#include <Arduino.h>
#include <Adafruit_LSM6DS3TRC.h>
#include <cmath>

// ============================================================================
// IMU RISE-TO-WAKE CALIBRATOR
// ============================================================================
// Captures acceleration data during user-performed "raise wrist" gestures
// and calculates the optimal wake-on-motion threshold

class IMUCalibrator {
public:
  IMUCalibrator();
  
  // Start calibration (user will perform gesture N times)
  void begin();
  
  // Update during calibration (call from main loop)
  void update(uint32_t now_ms);
  
  // Check if calibration is active
  bool isActive() const { return active; }
  
  // Get current progress (samples captured / total needed)
  uint8_t getProgress() const { return samples_captured; }
  
  // Finish calibration and calculate threshold
  uint8_t finalize();
  
  // Cancel calibration without saving
  void cancel();
  
  // Get the calculated threshold (after finalize)
  uint8_t getThreshold() const { return calculated_threshold; }

private:
  bool active;
  uint32_t start_time_ms;
  uint32_t last_reading_ms;
  uint8_t samples_captured;
  
  // Storage for max acceleration per gesture
  float gesture_max_accel[CALIBRATION_NUM_SAMPLES];
  
  // Current gesture being recorded
  float current_gesture_max;
  uint32_t gesture_start_ms;
  bool in_gesture;
  float last_accel_magnitude;
  
  // Result
  uint8_t calculated_threshold;
  
  // Reference to IMU
  Adafruit_LSM6DS3TRC imu;
  
  // Helper methods
  float _calculateAccelMagnitude(float x, float y, float z);
  bool _isGestureActive(float accel_mag, uint32_t now_ms);
  void _recordGestureEnd();
  uint8_t _calculateOptimalThreshold();
};

#endif // IMU_CALIBRATOR_H
