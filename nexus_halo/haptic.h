#ifndef HAPTIC_H
#define HAPTIC_H

#include "config.h"
#include <Arduino.h>

// ============================================================================
// HAPTIC CONTROLLER (Vibration Motor via D9 MOSFET)
// ============================================================================

typedef struct {
  uint16_t on_ms;
  uint16_t off_ms;
} VibrationStep;

enum HapticPattern {
  HAPTIC_PATTERN_RX,       // User received toque from pareja
  HAPTIC_PATTERN_TX,       // User sent touch to pareja
  HAPTIC_PATTERN_BATTERY,  // Low battery warning
  HAPTIC_PATTERN_ERROR,    // Error alert
  HAPTIC_PATTERN_NONE
};

class HapticController {
public:
  HapticController();
  
  // Initialize motor pin
  void begin();
  
  // Play a pattern (blocks until finished or interrupted)
  void playPattern(HapticPattern pattern);
  
  // Direct motor control (PWM intensity 0-255)
  void setMotor(uint8_t intensity);
  void stopMotor();
  
  // Check if motor is currently vibrating
  bool isVibrating() const { return vibrating; }
  
  // Update function for non-blocking pattern playback
  void update(uint32_t now_ms);
  
  // Interrupt current pattern and stop
  void stopPattern();

private:
  int pin_motor;
  bool vibrating;
  
  // Pattern playback state
  HapticPattern current_pattern;
  uint32_t pattern_start_ms;
  uint32_t step_start_ms;
  uint32_t current_step;
  bool pattern_active;
  
  // Pattern definitions
  static const VibrationStep PATTERN_RX[];
  static const VibrationStep PATTERN_TX[];
  static const VibrationStep PATTERN_BATTERY[];
  static const VibrationStep PATTERN_ERROR[];
  
  // Helper methods
  uint32_t _getPatternLength(HapticPattern pattern) const;
  const VibrationStep* _getPattern(HapticPattern pattern) const;
};

#endif // HAPTIC_H
