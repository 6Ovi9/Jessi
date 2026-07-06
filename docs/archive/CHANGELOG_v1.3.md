# Nexus Halo Firmware v1.3 — Changelog

## Overview
Firmware adaptation from v1.2 to v1.3 to fix critical bugs involving battery drain during deep sleep, TWI driver freezes, and non-blocking state machine deadlocks. The hardware documentation was also updated to reflect current physical board constraints.

## Major Changes & Bug Fixes

### 1. `DEEP_SLEEP` Battery Drain Fix
- **Bug**: `handleStateDeepSleep()` was being called on every 10ms loop tick because `state_machine.update()` is intentionally bypassed during deep sleep (via early return). The `state_machine.stateChanged()` check therefore always returned `true`, causing repeated executions of peripheral shutdown and IMU `Rise-to-Wake` configuration at 100Hz.
- **Fix**: Replaced the `stateChanged()` check with a local `static bool sleep_entry_done` guard that is reset to `false` ONLY when exiting the sleep state. The IMU and peripherals are now configured exactly once per sleep entry.

### 2. Compass TWI Driver Re-initialization Freeze
- **Bug**: During non-blocking recovery (`updateReinitSM()` in `compass.cpp`), Step 2 was calling `Wire.begin()` BEFORE enabling internal pull-ups. Because the physical board lacks external pull-ups on D4/D5, the nRF52840 TWI hardware read the bus as LOW during initialization and became permanently stuck.
- **Fix**: Moved `pinMode(..., INPUT_PULLUP)` commands *before* `Wire.begin()` in the non-blocking state machine to match the correct behavior established in `CompassController::begin()`.

### 3. Compass Update Deadlock
- **Bug**: When 3 zero readings were detected, `_readRaw()` triggered the non-blocking recovery by setting `_reinit_active = true` and `sensor_connected = false`. However, `update()` had an early return at the very top: `if (!sensor_connected) return;`. This caused `update()` to skip over `_updateReinitSM()` entirely, leaving the state machine frozen at Step 1 forever.
- **Fix**: Moved the `if (_reinit_active)` check to the absolute top of `update()`, bypassing the `sensor_connected` and update rate limiters. `sensor_connected = false` now properly triggers an early return *after* the non-blocking state machine has a chance to execute.

## Hardware Updates & Constraints

- **TTP223 Capacitive Button (D8)**: Known hardware issue where placement near battery pads causes false readings or total failure. Firmware configuration (`BUTTON_WAKE_ENABLED`) defaults to 0 to bypass this, relying entirely on the LSM6DS3 IMU for wake-up gestures (wrist flicks).
- **LIS3MDL Magnetometer (D4/D5)**: Known hardware issue where external I2C pull-ups are missing. The firmware now enforces strict internal pull-up initialization logic across all compass states to ensure the bus idles HIGH before the TWI driver takes over.

## Recent Fixes (v1.3.1)

### 4. I2C Hardware Lockup Safety Net
- **Bug**: The Seeed TWIM hardware driver locks up indefinitely if a slave stretches the clock or NAKs unexpectedly, hanging the entire watch.
- **Fix**: Reverted `Wire.endTransmission(false)` (Repeated Start) to `true` (STOP condition) which the nRF52 handles much better. Implemented `Wire.setTimeout(3)` on both buses to ensure the CPU always aborts a stuck transaction after 3ms, preventing total system freezes.

### 5. EEPROM Calibration Data Loss (Padding Bug)
- **Bug**: The C++ compiler injects invisible padding bytes into `CalibrationData` and `CompassCalibData` structs for memory alignment. Since these padding bytes were uninitialized, they contained random RAM garbage, causing the EEPROM checksum to fail on every reboot.
- **Fix**: Added strict `memset(&data, 0, sizeof(data))` to all EEPROM write functions (`eeprom_manager.cpp`, `compass.cpp`, `runtime_config.cpp`) to guarantee deterministic padding bytes before checksumming and saving.

### 6. Compass Startup Glitch
- **Bug**: Waking from deep sleep caused the compass heading to glitch because the median filter array contained zeroes or stale data.
- **Fix**: Injected an inline pre-fill loop in `_configure()` that calls the raw read and tilt compensation sequence 5 times to completely populate `history_x` and `history_y` before the sensor enters continuous operation.

### 7. Haptic UI Desync
- **Bug**: The `STATE_HAPTIC_TX` timeout was hardcoded to 3000ms, trapping the user in an unresponsive UI state for 3 seconds instead of the actual 400ms vibration duration. It also hardcoded the return state to `STATE_CLOCK_CONNECTED`, breaking UI flow if triggered while disconnected.
- **Fix**: Corrected the timeout to 500ms and updated the state machine to dynamically return to `previous_state`.
