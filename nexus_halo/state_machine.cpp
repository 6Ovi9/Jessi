#include "state_machine.h"
#include "runtime_config.h"
#include "gesture.h"

extern RuntimeConfigManager runtime_config;
extern GestureDetector gesture_detector;

StateMachine::StateMachine()
  : current_state(STATE_DEEP_SLEEP),
    previous_state(STATE_DEEP_SLEEP),
    state_changed(false),
    state_just_changed(false),
    state_entered_ms(0),
    timer_start_ms(0),
    timer_duration_ms(0),
    timer_active(false),
    low_battery_active(false),
    low_battery_pulse_last_ms((uint32_t)-TIMER_LOW_BATTERY_PULSE_MS),
    _low_battery_cb(nullptr)
{
}

void StateMachine::begin() {
  // Start in DEEP_SLEEP
  current_state = STATE_DEEP_SLEEP;
  previous_state = STATE_DEEP_SLEEP;
  state_entered_ms = millis();
  timer_active = false;
}

void StateMachine::update(uint32_t stale_now_ms) {
  // Explicitly refresh now_ms to prevent underflow from stale value passed by loop
  // caused by heavy blocking transitions
  uint32_t now_ms = millis();
  
  // Check timer expiration (overflow-safe)
  if (timer_active && (now_ms - timer_start_ms >= timer_duration_ms)) {
    timer_active = false;
    // Transition back based on current state logic
    switch (current_state) {
      case STATE_CLOCK_CONNECTED:
      case STATE_CLOCK_DISCONNECTED:
        transitionTo(STATE_DEEP_SLEEP);
        break;
      case STATE_CALIBRATION_MODE:
        transitionTo(previous_state);
        break;
      case STATE_RADAR_MODE:
      case STATE_DISTANCE_MODE:
        transitionTo(previous_state);
        break;
      case STATE_HAPTIC_RX:
        transitionTo(previous_state);
        break;
      case STATE_HAPTIC_TX:
        transitionTo(previous_state);
        break;
      case STATE_ERROR_NO_GPS:
        transitionTo(previous_state);
        break;
      case STATE_WAKING_UP:
        // WAKING_UP timed out (failed to init hardware). Fall back to DEEP_SLEEP.
        transitionTo(STATE_DEEP_SLEEP);
        break;
      case STATE_BATTERY_DEAD_DISPLAY:
        transitionTo(STATE_DEEP_SLEEP);
        break;
      default:
        break;
    }
  }
  
  // Handle LOW_BATTERY overlay pulse (every 30 seconds)
  if (low_battery_active) {
    if ((now_ms - low_battery_pulse_last_ms) >= TIMER_LOW_BATTERY_PULSE_MS) {
      low_battery_pulse_last_ms = now_ms;
      if (_low_battery_cb) _low_battery_cb();
    }
  }
}

void StateMachine::transitionTo(State new_state) {
  if (new_state == current_state) {
    if (new_state == STATE_HAPTIC_RX || new_state == STATE_HAPTIC_TX || new_state == STATE_BATTERY_DEAD_DISPLAY) {
      _enterState(new_state);
      state_changed = true;
      state_just_changed = true;
    }
    return;  // Already in this state
  }
  
  _exitState();
  if (new_state == STATE_HAPTIC_RX || new_state == STATE_HAPTIC_TX) {
    previous_state = (current_state == STATE_DEEP_SLEEP) ? STATE_CLOCK_CONNECTED : current_state;
  } else if (current_state != STATE_WAKING_UP && current_state != STATE_BATTERY_DEAD_DISPLAY && current_state != STATE_LOW_BATTERY && current_state != STATE_HAPTIC_RX && current_state != STATE_HAPTIC_TX && current_state != STATE_ERROR_NO_GPS) {
    previous_state = current_state;
  }
  current_state = new_state;
  _enterState(new_state);
  state_changed = true;
  state_just_changed = true;
}

void StateMachine::transitionToWithTimeout(State new_state, uint32_t timeout_ms) {
  transitionTo(new_state);
  resetTimer(timeout_ms);
}

void StateMachine::resetTimer(uint32_t timeout_ms) {
  if (timeout_ms == 0) { timer_active = false; return; }
  uint32_t now_ms = millis();
  timer_duration_ms = timeout_ms;
  timer_start_ms = now_ms;
  timer_active = true;
}

bool StateMachine::isTimerExpired() const {
  if (!timer_active) return false;
  return millis() - timer_start_ms >= timer_duration_ms;
}

uint32_t StateMachine::getTimerRemaining() const {
  if (!timer_active) return 0;
  uint32_t now_ms = millis();
  uint32_t elapsed = now_ms - timer_start_ms;
  if (elapsed >= timer_duration_ms) return 0;
  return timer_duration_ms - elapsed;
}

void StateMachine::_enterState(State new_state) {
  uint32_t now_ms = millis();
  state_entered_ms = now_ms;
  
  gesture_detector.reset();
  
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
      return (uint32_t)runtime_config.getConfig().clockTimeoutS * 1000;
    case STATE_RADAR_MODE:
    case STATE_DISTANCE_MODE:
      return (uint32_t)runtime_config.getConfig().sleepTimeoutS * 1000;
    case STATE_ERROR_NO_GPS:
      return TIMER_ERROR_NO_GPS_MS;
    case STATE_WAKING_UP:
      return 3000;  // 3-second timeout to prevent infinite waking-up deadlock
    case STATE_BATTERY_DEAD_DISPLAY:
      return 2000;  // Show dead battery for 2 seconds
    case STATE_HAPTIC_RX:
      return TIMER_HAPTIC_RX_TIMEOUT_MS;
    case STATE_HAPTIC_TX:
      return TIMER_HAPTIC_TX_TIMEOUT_MS;  // 400ms pattern + 100ms margin
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
    case STATE_BATTERY_DEAD_DISPLAY: return "BATTERY_DEAD_DISPLAY";
    case STATE_LOW_BATTERY:          return "LOW_BATTERY";
    default:                         return "UNKNOWN";
  }
}
