#include "imu_calibrator.h"

IMUCalibrator::IMUCalibrator()
  : active(false),
    start_time_ms(0),
    last_reading_ms(0),
    samples_captured(0),
    current_gesture_max(0),
    gesture_start_ms(0),
    in_gesture(false),
    last_accel_magnitude(0),
    calculated_threshold(IMU_WAKE_UP_THS_DEFAULT)
{
  for (int i = 0; i < CALIBRATION_NUM_SAMPLES; i++) {
    gesture_max_accel[i] = 0;
  }
}

void IMUCalibrator::begin() {
  active = true;
  start_time_ms = millis();
  last_reading_ms = start_time_ms;
  samples_captured = 0;
  current_gesture_max = 0;
  gesture_start_ms = 0;
  in_gesture = false;
  last_accel_magnitude = 0;
  
  // Serial.println("[CALIB] Calibration started. Perform raise-to-wake gesture 5 times.");
}

void IMUCalibrator::update(uint32_t now_ms) {
  if (!active) return;
  
  // Check timeout
  if ((now_ms - start_time_ms) > CALIBRATION_TIMEOUT_MS) {
    // Serial.println("[CALIB] Calibration timeout");
    finalize();
    return;
  }
  
  // Read IMU at ~10Hz during calibration
  if ((now_ms - last_reading_ms) < 100) {
    return;  // Not time yet
  }
  last_reading_ms = now_ms;
  
  // Get acceleration data
  sensors_event_t accel;
  imu.getAcceleration_ms2(&accel.acceleration.x,
                          &accel.acceleration.y,
                          &accel.acceleration.z);
  
  float accel_mag = _calculateAccelMagnitude(accel.acceleration.x,
                                             accel.acceleration.y,
                                             accel.acceleration.z);
  
  // Check if gesture is active
  if (_isGestureActive(accel_mag, now_ms)) {
    if (!in_gesture) {
      // Start of new gesture
      in_gesture = true;
      gesture_start_ms = now_ms;
      current_gesture_max = accel_mag;
      // Serial.println("[CALIB] Gesture detected");
    } else {
      // Ongoing gesture - track max acceleration
      if (accel_mag > current_gesture_max) {
        current_gesture_max = accel_mag;
      }
    }
  } else {
    // Gesture ended (or never started)
    if (in_gesture) {
      _recordGestureEnd();
    }
  }
  
  last_accel_magnitude = accel_mag;
}

float IMUCalibrator::_calculateAccelMagnitude(float x, float y, float z) {
  // Magnitude in m/s²
  return sqrt(x*x + y*y + z*z);
}

bool IMUCalibrator::_isGestureActive(float accel_mag, uint32_t now_ms) {
  // A gesture is active if:
  // 1. Current acceleration > threshold, OR
  // 2. We're already recording and haven't gone below threshold for 200ms
  
  if (accel_mag >= (CALIBRATION_MIN_ACCEL_MG / 1000.0f)) {
    return true;  // Strong motion
  }
  
  if (in_gesture) {
    // Check if gesture has ended (low motion for 200ms)
    if ((now_ms - gesture_start_ms) > 200) {
      return (accel_mag >= (CALIBRATION_MIN_ACCEL_MG / 1000.0f * 0.5f));
    }
    return true;  // Still in potential gesture window
  }
  
  return false;
}

void IMUCalibrator::_recordGestureEnd() {
  if (samples_captured < CALIBRATION_NUM_SAMPLES) {
    gesture_max_accel[samples_captured] = current_gesture_max;
    samples_captured++;
    
    // Convert to mg for logging
    float max_accel_mg = current_gesture_max * 1000.0f;
    // Serial.print("[CALIB] Gesture ");
    // Serial.print(samples_captured);
    // Serial.print(" recorded: ");
    // Serial.print(max_accel_mg, 1);
    // Serial.println(" mg");
    
    if (samples_captured >= CALIBRATION_NUM_SAMPLES) {
      // Serial.println("[CALIB] All samples captured!");
      finalize();
      return;
    }
  }
  
  in_gesture = false;
  current_gesture_max = 0;
}

uint8_t IMUCalibrator::finalize() {
  active = false;
  calculated_threshold = _calculateOptimalThreshold();
  
  // Serial.print("[CALIB] Calibration complete. Threshold: 0x");
  // Serial.println(calculated_threshold, HEX);
  
  return calculated_threshold;
}

void IMUCalibrator::cancel() {
  active = false;
  samples_captured = 0;
}

uint8_t IMUCalibrator::_calculateOptimalThreshold() {
  if (samples_captured == 0) {
    return IMU_WAKE_UP_THS_DEFAULT;
  }
  
  // Find minimum max acceleration from all captured gestures
  // This becomes our threshold (with some safety margin)
  float min_accel = gesture_max_accel[0];
  for (int i = 1; i < samples_captured; i++) {
    if (gesture_max_accel[i] < min_accel) {
      min_accel = gesture_max_accel[i];
    }
  }
  
  // Convert to mg
  float min_accel_mg = min_accel * 1000.0f;
  
  // Apply safety margin (80% of minimum detected)
  float threshold_mg = min_accel_mg * 0.8f;
  
  // Convert mg to WAKE_UP_THS register value
  // At ±2G range: 1 LSB ≈ 15.625mg
  // threshold_reg = threshold_mg / 15.625
  
  float threshold_reg_f = threshold_mg / 15.625f;
  uint8_t threshold_reg = (uint8_t)threshold_reg_f;
  
  // Clamp to valid range
  if (threshold_reg > IMU_WAKE_UP_THS_MAX) threshold_reg = IMU_WAKE_UP_THS_MAX;
  if (threshold_reg < IMU_WAKE_UP_THS_MIN) threshold_reg = IMU_WAKE_UP_THS_MIN;
  
  // Serial.print("[CALIB] Min acceleration: ");
  // Serial.print(min_accel_mg, 1);
  // Serial.print(" mg → Threshold: ");
  // Serial.print(threshold_mg, 1);
  // Serial.print(" mg → Register: 0x");
  // Serial.println(threshold_reg, HEX);
  
  return threshold_reg;
}
