#include "gesture.h"

GestureDetector::GestureDetector()
  : imu_ptr(nullptr),
    last_flick_ms(0),
    flick_count(0),
    flick_reset(true),
    gyro_threshold(GESTURE_GYRO_THS_DEFAULT),
    double_flick_window(GESTURE_DOUBLE_FLICK_WINDOW_DEFAULT),
    detected_gesture(GESTURE_NONE),
    gesture_reported(true)
{
}

void GestureDetector::begin() {
  last_flick_ms = 0;
  flick_count = 0;
  flick_reset = true;
  detected_gesture = GESTURE_NONE;
  gesture_reported = true;
  sequence_start_ms = 0;
}

void GestureDetector::update(uint32_t now_ms) {
  // Check multi-flick timeout: if we were waiting for more flicks but no more came within the window,
  // report the accumulated flicks (1 flick = GESTURE_TAP_SIMPLE, 2 flicks = GESTURE_TAP_DOUBLE)
  if (flick_count > 0 && (now_ms - sequence_start_ms) > double_flick_window) {
    if (gesture_reported || detected_gesture == GESTURE_NONE) {
      if (flick_count == 1) detected_gesture = GESTURE_TAP_SIMPLE;
      else if (flick_count == 2) detected_gesture = GESTURE_TAP_DOUBLE;
      gesture_reported = false;
    }
    flick_count = 0;
  }

  // If we have a reference to the IMU, use gyroscope flick detection
  if (imu_ptr) {
    float gx = imu_ptr->readFloatGyroX();
    float gy = imu_ptr->readFloatGyroY();
    float gz = imu_ptr->readFloatGyroZ();
    
    float gyro_mag = std::sqrt(gx*gx + gy*gy + gz*gz);
    
    static uint8_t tear_debounce = 0;
    if (gyro_mag > gyro_threshold) {
      tear_debounce++;
      if (tear_debounce < 2) return;
    } else {
      tear_debounce = 0;
    }
    
    // We detect a flick if the gyro magnitude exceeds a threshold
    // and we haven't detected a flick recently (e.g. dynamic cooldown to allow fast flicks)
    // and the sensor returned to a quiet state (< 80% threshold) since the last flick.
    uint32_t cooldown = double_flick_window / 2 < 280 ? double_flick_window / 2 : 280;
    if (gyro_mag > gyro_threshold && (now_ms - last_flick_ms) > cooldown && flick_reset) {
      uint32_t prev_flick_ms = last_flick_ms;
      last_flick_ms = now_ms;
      flick_reset = false;
      Serial.print("[GESTURE] Gyro flick detected: ");
      Serial.println(gyro_mag);
      
      if (flick_count == 0) {
        flick_count = 1;
        sequence_start_ms = now_ms;
      } else {
        uint32_t gap = now_ms - prev_flick_ms;
        if (gap <= double_flick_window) {
          flick_count++;
          if (flick_count >= 3) {
            if (gesture_reported || detected_gesture == GESTURE_NONE) {
              detected_gesture = GESTURE_TAP_TRIPLE;
              gesture_reported = false;
              Serial.println("[GESTURE] GESTURE_TAP_TRIPLE (Triple Flick)");
            }
            flick_count = 0;
          }
        }
      }
    }
    // Reset quiet state flag if the gyro magnitude falls below quiet threshold
    if (gyro_mag < fmax(gyro_threshold * 0.8f, 50.0f)) {
      flick_reset = true;
    }
  }
  
}

// BUG-029: reset() intentionally drops any gesture in progress.
// This is called when entering a new state and any pending gesture from the
// previous state should be discarded to prevent cross-state gesture leakage.
void GestureDetector::reset() {
  flick_count = 0;
  detected_gesture = GESTURE_NONE;
  gesture_reported = true;
  last_flick_ms = 0;
  sequence_start_ms = 0;
  flick_reset = true;
}


GestureType GestureDetector::getGesture() {
  if (gesture_reported || detected_gesture == GESTURE_NONE) {
    return GESTURE_NONE;
  }
  
  gesture_reported = true;
  return detected_gesture;
}

