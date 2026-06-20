#ifndef GESTURE_H
#define GESTURE_H

#include "config.h"
#include <Arduino.h>

// ============================================================================
// GESTURE DETECTION (TTP223 Button)
// ============================================================================
// Debounced button input with multi-tap gesture recognition

class GestureDetector {
public:
  GestureDetector();
  
  // Initialize (set pin, attach interrupt if needed)
  void begin();
  
  // Call frequently from loop (~10ms)
  void update(uint32_t now_ms);
  
  // Get detected gesture (clears after reading)
  GestureType getGesture();
  
  // Raw button state
  bool isButtonPressed() const { return button_pressed; }
  
  // Debug info
  uint32_t getButtonPressDuration() const;

private:
  // Pin and state
  int pin_button;
  bool button_pressed;
  bool button_pressed_last;
  
  // Debounce
  uint32_t last_edge_ms;
  
  // Tap detection
  uint32_t tap_start_ms;
  uint32_t tap_end_ms;
  bool tap_in_progress;
  
  // Double-tap detection
  uint32_t first_tap_end_ms;
  bool waiting_for_second_tap;
  
  // Gesture result
  GestureType detected_gesture;
  bool gesture_reported;
  
  // Helper methods
  void _onButtonDown(uint32_t now_ms);
  void _onButtonUp(uint32_t now_ms);
  void _evaluateGesture();
};

#endif // GESTURE_H
