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
    detected_gesture(GESTURE_NONE),
    gesture_reported(true)
{
}

void GestureDetector::begin() {
  pinMode(pin_button, INPUT);
  button_pressed = false;
  button_pressed_last = false;
  tap_in_progress = false;
  waiting_for_second_tap = false;
  detected_gesture = GESTURE_NONE;
  gesture_reported = true;
}

void GestureDetector::update(uint32_t now_ms) {
  // Read button (debounced)
  bool button_raw = digitalRead(pin_button) == HIGH;
  
  // Apply debounce
  if (button_raw != button_pressed_last) {
    if ((now_ms - last_edge_ms) >= DEBOUNCE_MS) {
      button_pressed = button_raw;
      button_pressed_last = button_raw;
      last_edge_ms = now_ms;
      
      // Rising edge (button pressed down)
      if (button_pressed) {
        _onButtonDown(now_ms);
      }
      // Falling edge (button released)
      else {
        _onButtonUp(now_ms);
      }
    }
  }
  
  // Check for long press (if still held)
  if (tap_in_progress && button_pressed) {
    uint32_t hold_time = now_ms - tap_start_ms;
    
    // Press long detected (but we'll wait for release to confirm)
    // For now, we only detect on release
  }
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
