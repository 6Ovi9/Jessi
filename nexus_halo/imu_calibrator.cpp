#include "imu_calibrator.h"

IMUCalibrator::IMUCalibrator()
  : active(false),
    start_time_ms(0),
    last_reading_ms(0),
    samples_captured(0),
    current_gesture_max(0),
    gesture_start_ms(0),
    in_gesture(false),
    prev_x(0),
    prev_y(0),
    prev_z(0),
    last_motion_ms(0),
    calculated_threshold(IMU_WAKE_UP_THS_DEFAULT),
    imu_ptr(nullptr)  // Initialize pointer to null
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
  prev_x = 0;
  prev_y = 0;
  prev_z = 0;
  last_motion_ms = 0;
  
  // IMU should already be initialized in setup()
  // No need to initialize Wire or IMU here since imu_ptr is set externally
  
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
  
  // Read IMU at ~100Hz during calibration
  if ((now_ms - last_reading_ms) < 10) {
    return;  // Not time yet
  }
  last_reading_ms = now_ms;
  
  // Get acceleration data (Seeed API)
  if (!imu_ptr) {
    return;  // IMU not set
  }
  
  float accel_x = imu_ptr->readFloatAccelX();
  float accel_y = imu_ptr->readFloatAccelY();
  float accel_z = imu_ptr->readFloatAccelZ();
  
  float delta_x = fabs(accel_x - prev_x);
  float delta_y = fabs(accel_y - prev_y);
  float delta_z = fabs(accel_z - prev_z);
  float dynamic_accel = (prev_x == 0 && prev_y == 0 && prev_z == 0) ? 0 : fmax(delta_x, fmax(delta_y, delta_z));
  prev_x = accel_x;
  prev_y = accel_y;
  prev_z = accel_z;
  
  // Check if gesture is active
  if (_isGestureActive(dynamic_accel, now_ms)) {
    if (!in_gesture) {
      // Start of new gesture
      in_gesture = true;
      gesture_start_ms = now_ms;
      current_gesture_max = dynamic_accel;
      // Serial.println("[CALIB] Gesture detected");
    } else {
      // Ongoing gesture - track max acceleration
      if (dynamic_accel > current_gesture_max) {
        current_gesture_max = dynamic_accel;
      }
    }
  } else {
    // Gesture ended (or never started)
    if (in_gesture) {
      _recordGestureEnd();
    }
  }
  
  // removed last_accel_magnitude
}

float IMUCalibrator::_calculateAccelMagnitude(float x, float y, float z) {
  // Magnitude in m/s²
  return sqrt(x*x + y*y + z*z);
}

bool IMUCalibrator::_isGestureActive(float dynamic_accel, uint32_t now_ms) {
  // A gesture is active if:
  // 1. Current dynamic acceleration > threshold, OR
  // 2. We're already recording and haven't gone below threshold for 200ms
  
  if (dynamic_accel >= (CALIBRATION_MIN_ACCEL_MG / 1000.0f)) {
    last_motion_ms = now_ms;
    return true;  // Strong motion
  }
  
  if (in_gesture) {
    if ((now_ms - gesture_start_ms) > 2000) return false;  // BUG-036: max 2s gesture duration
    if ((now_ms - last_motion_ms) > 200) {
      return false;
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
      return;  // BUG-035: Must not fall through to in_gesture = false after finalize()
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
  // At ±2G range: 1 LSB = 62.5mg (nominal scale for settings)
  // threshold_reg = threshold_mg / 62.5
  
  float threshold_reg_f = threshold_mg / 31.25f;
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
