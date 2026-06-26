#ifndef STATE_MACHINE_H
#define STATE_MACHINE_H

#include "config.h"
#include <stdint.h>
#include <Arduino.h>

// ============================================================================
// STATE MACHINE
// ============================================================================
// Manages all state transitions and timers

class StateMachine {
public:
  StateMachine();
  
  // Initialize the state machine
  void begin();
  
  // Update function (call from main loop frequently, ~10ms)
  void update(uint32_t now_ms);
  
  // Get current state
  State getCurrentState() const { return current_state; }
  
  // Request state transitions
  void transitionTo(State new_state);
  void transitionToIfConnected(State new_state);  // Only if BLE connected
  void transitionToWithTimeout(State new_state, uint32_t timeout_ms);
  
  // Check if state changed this update
  bool stateChanged() { return state_changed; }
  
  // Timer management
  void resetTimer(uint32_t timeout_ms);
  bool isTimerExpired() const;
  uint32_t getTimerRemaining() const;
  
  // Low battery overlay (doesn't change current_state)
  void setLowBatteryActive(bool active) { low_battery_active = active; }
  bool isLowBatteryActive() const { return low_battery_active; }
  
  // Wake source tracking (for rise-to-wake diagnostics)
  int getLastWakeSource() const { return last_wake_source; }
  void setWakeSource(int source) { last_wake_source = source; }
  
  // Get state name (for debugging)
  const char* getStateName() const;
  const char* getStateName(State state) const;

private:
  State current_state;
  State previous_state;
  State target_state;           // Used for transitions
  bool state_changed;
  
  uint32_t state_entered_ms;    // Timestamp when current state was entered
  uint32_t timer_start_ms;      // Timestamp when the current timer started
  uint32_t timer_duration_ms;   // How long the timer is set for
  bool timer_active;
  
  bool low_battery_active;      // Overlay flag (doesn't change state)
  uint32_t low_battery_pulse_last_ms;
  int last_wake_source;         // WAKE_SOURCE_NONE, WAKE_SOURCE_TAP, or WAKE_SOURCE_MOTION
  
  // Helper for state transitions
  void _enterState(State new_state);
  void _exitState();
  
  // Default timeout based on state
  uint32_t _getDefaultTimeout(State state) const;
};

#endif // STATE_MACHINE_H
