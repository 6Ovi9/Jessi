#include "state_machine.h"

StateMachine::StateMachine()
  : current_state(STATE_DEEP_SLEEP),
    previous_state(STATE_DEEP_SLEEP),
    target_state(STATE_DEEP_SLEEP),
    state_changed(false),
    state_entered_ms(0),
    timer_expire_ms(0),
    timer_duration_ms(0),
    timer_active(false),
    low_battery_active(false),
    low_battery_pulse_last_ms(0),
    last_wake_source(WAKE_SOURCE_NONE)
{
}

void StateMachine::begin() {
  // Start in DEEP_SLEEP
  current_state = STATE_DEEP_SLEEP;
  previous_state = STATE_DEEP_SLEEP;
  state_entered_ms = millis();
  timer_active = false;
}

void StateMachine::update(uint32_t now_ms) {
  state_changed = false;
  
  // Check timer expiration
  if (timer_active && (now_ms >= timer_expire_ms)) {
    timer_active = false;
    // Transition back based on current state logic
    switch (current_state) {
      case STATE_CLOCK_CONNECTED:
      case STATE_CLOCK_DISCONNECTED:
        transitionTo(STATE_DEEP_SLEEP);
        break;
      case STATE_RADAR_MODE:
      case STATE_DISTANCE_MODE:
        transitionTo(STATE_CLOCK_CONNECTED);
        break;
      case STATE_HAPTIC_RX:
        transitionTo(previous_state);
        break;
      case STATE_ERROR_NO_GPS:
        transitionTo(STATE_CLOCK_CONNECTED);
        break;
      case STATE_WAKING_UP:
        // WAKING_UP should be handled by gesture/BLE callback, not timeout
        break;
      default:
        break;
    }
  }
  
  // Handle LOW_BATTERY overlay pulse (every 30 seconds)
  if (low_battery_active) {
    if ((now_ms - low_battery_pulse_last_ms) >= TIMER_LOW_BATTERY_PULSE_MS) {
      low_battery_pulse_last_ms = now_ms;
      // Pulse motor/LED once (handled by haptic_controller and led_controller)
    }
  }
}

void StateMachine::transitionTo(State new_state) {
  if (new_state == current_state) {
    return;  // Already in this state
  }
  
  _exitState();
  previous_state = current_state;
  current_state = new_state;
  _enterState(new_state);
  state_changed = true;
}

void StateMachine::transitionToIfConnected(State new_state) {
  // This would check BLE connection status
  // For now, just do a regular transition
  // The caller (gesture handler) should verify connectivity
  transitionTo(new_state);
}

void StateMachine::transitionToWithTimeout(State new_state, uint32_t timeout_ms) {
  transitionTo(new_state);
  resetTimer(timeout_ms);
}

void StateMachine::resetTimer(uint32_t timeout_ms) {
  uint32_t now_ms = millis();
  timer_duration_ms = timeout_ms;
  timer_expire_ms = now_ms + timeout_ms;
  timer_active = true;
}

bool StateMachine::isTimerExpired() const {
  if (!timer_active) return false;
  return millis() >= timer_expire_ms;
}

uint32_t StateMachine::getTimerRemaining() const {
  if (!timer_active) return 0;
  uint32_t now_ms = millis();
  if (now_ms >= timer_expire_ms) return 0;
  return timer_expire_ms - now_ms;
}

void StateMachine::_enterState(State new_state) {
  uint32_t now_ms = millis();
  state_entered_ms = now_ms;
  
  // Set default timeout based on state
  uint32_t default_timeout = _getDefaultTimeout(new_state);
  if (default_timeout > 0) {
    resetTimer(default_timeout);
  } else {
    timer_active = false;
  }
}

void StateMachine::_exitState() {
  // Cleanup for exiting current state (if needed)
}

uint32_t StateMachine::_getDefaultTimeout(State state) const {
  switch (state) {
    case STATE_CLOCK_CONNECTED:
    case STATE_CLOCK_DISCONNECTED:
      return TIMER_CLOCK_TIMEOUT_MS;
    case STATE_RADAR_MODE:
    case STATE_DISTANCE_MODE:
      return TIMER_RADAR_TIMEOUT_MS;
    case STATE_ERROR_NO_GPS:
      return TIMER_ERROR_NO_GPS_MS;
    case STATE_WAKING_UP:
      return TIMER_WAKING_UP_MS;
    case STATE_HAPTIC_RX:
      return TIMER_HAPTIC_RX_TIMEOUT_MS;
    default:
      return 0;  // No default timeout
  }
}

const char* StateMachine::getStateName() const {
  return getStateName(current_state);
}

const char* StateMachine::getStateName(State state) const {
  switch (state) {
    case STATE_DEEP_SLEEP:           return "DEEP_SLEEP";
    case STATE_WAKING_UP:            return "WAKING_UP";
    case STATE_CLOCK_CONNECTED:      return "CLOCK_CONNECTED";
    case STATE_CLOCK_DISCONNECTED:   return "CLOCK_DISCONNECTED";
    case STATE_RADAR_MODE:           return "RADAR_MODE";
    case STATE_DISTANCE_MODE:        return "DISTANCE_MODE";
    case STATE_HAPTIC_TX:            return "HAPTIC_TX";
    case STATE_HAPTIC_RX:            return "HAPTIC_RX";
    case STATE_OTA_MODE:             return "OTA_MODE";
    case STATE_ERROR_NO_GPS:         return "ERROR_NO_GPS";
    case STATE_CALIBRATION_MODE:     return "CALIBRATION_MODE";
    case STATE_LOW_BATTERY:          return "LOW_BATTERY";
    default:                         return "UNKNOWN";
  }
}
