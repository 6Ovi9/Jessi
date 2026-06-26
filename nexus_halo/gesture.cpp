#include "gesture.h"

GestureDetector::GestureDetector()
  : pin_button(PIN_BUTTON_TOUCH),
    button_pressed(false),
    button_pressed_last(false),
    last_edge_ms(0),
    tap_start_ms(0),
    tap_end_ms(0),
    tap_in_progress(false),
    first_tap_end_ms(0),
    waiting_for_second_tap(false),
    imu_ptr(nullptr),
    last_flick_ms(0),
    first_flick_ms(0),
    waiting_for_second_flick(false),
    flick_reset(true),
    gyro_threshold(GESTURE_GYRO_THS_DEFAULT),
    detected_gesture(GESTURE_NONE),
    gesture_reported(true)
{
}

void GestureDetector::begin() {
  pinMode(pin_button, INPUT_PULLDOWN);
  button_pressed = false;
  button_pressed_last = false;
  tap_in_progress = false;
  waiting_for_second_tap = false;
  last_flick_ms = 0;
  first_flick_ms = 0;
  waiting_for_second_flick = false;
  flick_reset = true;
  detected_gesture = GESTURE_NONE;
  gesture_reported = true;
}

void GestureDetector::update(uint32_t now_ms) {
  // If we have a reference to the IMU, use gyroscope flick detection
  if (imu_ptr) {
    float gx = imu_ptr->readFloatGyroX();
    float gy = imu_ptr->readFloatGyroY();
    float gz = imu_ptr->readFloatGyroZ();
    
    float gyro_mag = sqrt(gx*gx + gy*gy + gz*gz);
    
    // We detect a flick if the gyro magnitude exceeds a threshold
    // and we haven't detected a flick recently (e.g. last 400ms cooldown)
    // and the sensor returned to a quiet state (< 80 dps) since the last flick.
    if (gyro_mag > gyro_threshold && (now_ms - last_flick_ms) > 400 && flick_reset) {
      last_flick_ms = now_ms;
      flick_reset = false;
      Serial.print("[GESTURE] Gyro flick detected: ");
      Serial.println(gyro_mag);
      
      if (waiting_for_second_flick) {
        // Double flick!
        uint32_t gap = now_ms - first_flick_ms;
        if (gap <= 800) {
          detected_gesture = GESTURE_TAP_DOUBLE;
          gesture_reported = false;
          waiting_for_second_flick = false;
          Serial.println("[GESTURE] GESTURE_TAP_DOUBLE (Double Flick)");
        } else {
          // Gap too long, treat as first flick of a new potential double flick
          first_flick_ms = now_ms;
          waiting_for_second_flick = true;
        }
      } else {
        first_flick_ms = now_ms;
        waiting_for_second_flick = true;
      }
    }
    
    // Reset quiet state flag if the gyro magnitude falls below quiet threshold
    if (gyro_mag < 80.0f) {
      flick_reset = true;
    }
  }
  
  // Check double-flick timeout: if we were waiting for a second flick
  // but it didn't come within 500ms, report a single flick (short press)!
  if (waiting_for_second_flick && (now_ms - first_flick_ms) > 500) {
    detected_gesture = GESTURE_PRESS_SHORT;
    gesture_reported = false;
    waiting_for_second_flick = false;
    Serial.println("[GESTURE] GESTURE_PRESS_SHORT (Single Flick)");
  }
}

void GestureDetector::reset() {
  last_flick_ms = 0;
  first_flick_ms = 0;
  waiting_for_second_flick = false;
  flick_reset = true;
  detected_gesture = GESTURE_NONE;
  gesture_reported = true;
}

void GestureDetector::_onButtonDown(uint32_t now_ms) {
  tap_start_ms = now_ms;
  tap_in_progress = true;
  detected_gesture = GESTURE_NONE;
  gesture_reported = false;
  
  // If we were waiting for a second tap and it came within window, mark it
  // But we'll confirm on release
}

void GestureDetector::_onButtonUp(uint32_t now_ms) {
  if (!tap_in_progress) {
    return;  // Spurious release
  }
  
  tap_end_ms = now_ms;
  uint32_t tap_duration = tap_end_ms - tap_start_ms;
  tap_in_progress = false;
  
  // Classify the tap/press
  if (tap_duration >= PRESS_LONG_MS) {
    // Press long
    detected_gesture = GESTURE_PRESS_LONG;
    gesture_reported = false;
    waiting_for_second_tap = false;  // Cancel double-tap wait
  }
  else if (tap_duration >= PRESS_SHORT_MS) {
    // Press short
    detected_gesture = GESTURE_PRESS_SHORT;
    gesture_reported = false;
    waiting_for_second_tap = false;  // Cancel double-tap wait
  }
  else if (tap_duration >= TAP_MIN_MS && tap_duration <= TAP_MAX_MS) {
    // Valid tap detected
    if (waiting_for_second_tap) {
      // Check if second tap is within window
      uint32_t gap = tap_start_ms - first_tap_end_ms;
      if (gap <= DOUBLE_TAP_WINDOW_MS) {
        // Double tap!
        detected_gesture = GESTURE_TAP_DOUBLE;
        gesture_reported = false;
        waiting_for_second_tap = false;
      } else {
        // Gap too long, treat as new single tap
        first_tap_end_ms = tap_end_ms;
        detected_gesture = GESTURE_TAP_SIMPLE;
        gesture_reported = false;
        waiting_for_second_tap = true;
      }
    } else {
      // First tap detected
      detected_gesture = GESTURE_TAP_SIMPLE;
      gesture_reported = false;
      first_tap_end_ms = tap_end_ms;
      waiting_for_second_tap = true;
    }
  }
  // else: tap too short, ignore
}

GestureType GestureDetector::getGesture() {
  if (gesture_reported || detected_gesture == GESTURE_NONE) {
    return GESTURE_NONE;
  }
  
  gesture_reported = true;
  return detected_gesture;
}

uint32_t GestureDetector::getButtonPressDuration() const {
  if (!tap_in_progress) {
    return 0;
  }
  return millis() - tap_start_ms;
}
