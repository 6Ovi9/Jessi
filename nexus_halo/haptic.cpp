#include "haptic.h"

// Pattern definitions (on_ms, off_ms)
const VibrationStep HapticController::PATTERN_RX[] = {
  {200, 100},
  {200, 100},
  {400, 0},
  {0, 0}  // Terminator
};

const VibrationStep HapticController::PATTERN_TX[] = {
  {100, 100},
  {100, 0},
  {0, 0}  // Terminator
};

const VibrationStep HapticController::PATTERN_BATTERY[] = {
  {100, 0},
  {0, 0}  // Terminator
};

const VibrationStep HapticController::PATTERN_ERROR[] = {
  {50, 50},
  {50, 50},
  {50, 0},
  {0, 0}  // Terminator
};

HapticController::HapticController()
  : pin_motor(PIN_MOTOR),
    vibrating(false),
    current_pattern(HAPTIC_PATTERN_NONE),
    pattern_start_ms(0),
    step_start_ms(0),
    current_step(0),
    pattern_active(false)
{
}

void HapticController::begin() {
  pinMode(pin_motor, OUTPUT);
  digitalWrite(pin_motor, LOW);
  vibrating = false;
}

void HapticController::playPattern(HapticPattern pattern) {
  current_pattern = pattern;
  pattern_start_ms = millis();
  step_start_ms = pattern_start_ms;
  current_step = 0;
  pattern_active = true;
  vibrating = false;
  
  // Start first step if it has a duration
  const VibrationStep* p = _getPattern(pattern);
  if (p && p[0].on_ms > 0) {
    setMotor(255);  // Full intensity
    vibrating = true;
  }
}

void HapticController::setMotor(uint8_t intensity) {
  vibrating = (intensity > 0);
  
  // For now, simple on/off; PWM could be implemented
  digitalWrite(pin_motor, vibrating ? HIGH : LOW);
}

void HapticController::stopMotor() {
  digitalWrite(pin_motor, LOW);
  vibrating = false;
  pattern_active = false;
}

void HapticController::update(uint32_t now_ms) {
  if (!pattern_active) return;
  
  const VibrationStep* pattern = _getPattern(current_pattern);
  if (!pattern) {
    pattern_active = false;
    return;
  }
  
  uint32_t elapsed_in_step = now_ms - step_start_ms;
  
  // Check if we're in the ON phase or OFF phase
  VibrationStep current = pattern[current_step];
  
  if (current.on_ms == 0 && current.off_ms == 0) {
    // End of pattern
    stopMotor();
    return;
  }
  
  // Determine if we should be vibrating
  bool should_vibrate = (elapsed_in_step < current.on_ms);
  
  // If we've completed this step, move to next
  uint32_t step_total = current.on_ms + current.off_ms;
  if (elapsed_in_step >= step_total) {
    current_step++;
    step_start_ms = now_ms;
    
    // Check for end of pattern
    VibrationStep next = pattern[current_step];
    if (next.on_ms == 0 && next.off_ms == 0) {
      stopMotor();
      return;
    }
    
    should_vibrate = (next.on_ms > 0);
  }
  
  // Apply motor state
  if (should_vibrate) {
    setMotor(255);
  } else {
    setMotor(0);
  }
}

void HapticController::stopPattern() {
  pattern_active = false;
  stopMotor();
}

uint32_t HapticController::_getPatternLength(HapticPattern pattern) const {
  const VibrationStep* p = _getPattern(pattern);
  if (!p) return 0;
  
  uint32_t total = 0;
  while (p->on_ms > 0 || p->off_ms > 0) {
    total += p->on_ms + p->off_ms;
    p++;
  }
  return total;
}

const VibrationStep* HapticController::_getPattern(HapticPattern pattern) const {
  switch (pattern) {
    case HAPTIC_PATTERN_RX:      return PATTERN_RX;
    case HAPTIC_PATTERN_TX:      return PATTERN_TX;
    case HAPTIC_PATTERN_BATTERY: return PATTERN_BATTERY;
    case HAPTIC_PATTERN_ERROR:   return PATTERN_ERROR;
    default:                      return nullptr;
  }
}
