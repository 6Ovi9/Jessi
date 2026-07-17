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
    pattern_active(false)
{
}

void HapticController::begin() {
  digitalWrite(pin_motor, LOW); // Mitigate floating gate before init
  pinMode(pin_motor, OUTPUT);
  digitalWrite(pin_motor, LOW);
  analogWrite(pin_motor, 0);
  vibrating = false;
  pattern_active = false;
}

void HapticController::playPattern(HapticPattern pattern) {
  if (pattern_active) {
    queued_pattern = pattern;
    return;
  }
  stopMotor();
  current_pattern = pattern;
  pattern_start_ms = millis();
  pattern_active = true;
  vibrating = false;
  

}

void HapticController::setMotor(uint8_t intensity) {
  bool new_vibrating = (intensity > 0);
  if (new_vibrating != vibrating) {
    vibrating = new_vibrating;
    // For now, simple on/off; PWM could be implemented
  }
  analogWrite(pin_motor, intensity);
}

void HapticController::stopMotor() {
  analogWrite(pin_motor, 0);
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
  
  uint32_t elapsed_total = now_ms - pattern_start_ms;
  uint32_t accum = 0;
  
  size_t max_steps = _getPatternSize(current_pattern);
  for (size_t i = 0; i < max_steps; i++) {
    VibrationStep step = pattern[i];
    if (step.on_ms == 0 && step.off_ms == 0) {
      stopMotor();
      pattern_active = false;
      if (queued_pattern != HAPTIC_PATTERN_NONE) {
        HapticPattern next = queued_pattern;
        queued_pattern = HAPTIC_PATTERN_NONE;
        playPattern(next);
      }
      return;
    }
    
    uint32_t step_len = (uint32_t)step.on_ms + step.off_ms;
    if (elapsed_total < accum + step_len) {
      bool should_vibrate = (elapsed_total < accum + step.on_ms);
      setMotor(should_vibrate ? 255 : 0);
      return;
    }
    accum += step_len;
  }
  // If we exceeded max steps without hitting terminator
  stopMotor();
  pattern_active = false;
}

void HapticController::stopPattern() {
  pattern_active = false;
  stopMotor();
}

uint32_t HapticController::getPatternLength(HapticPattern pattern) const {
  const VibrationStep* p = _getPattern(pattern);
  if (!p) return 0;
  
  uint32_t total = 0;
  size_t max_steps = _getPatternSize(pattern);
  for (size_t i = 0; i < max_steps; i++) {
    if (p[i].on_ms == 0 && p[i].off_ms == 0) break;
    total += (uint32_t)p[i].on_ms + p[i].off_ms;
  }
  return total;
}

size_t HapticController::_getPatternSize(HapticPattern pattern) const {
  switch (pattern) {
    case HAPTIC_PATTERN_RX:      return sizeof(PATTERN_RX)/sizeof(VibrationStep);
    case HAPTIC_PATTERN_TX:      return sizeof(PATTERN_TX)/sizeof(VibrationStep);
    case HAPTIC_PATTERN_BATTERY: return sizeof(PATTERN_BATTERY)/sizeof(VibrationStep);
    case HAPTIC_PATTERN_ERROR:   return sizeof(PATTERN_ERROR)/sizeof(VibrationStep);
    default:                     return 0;
  }
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
