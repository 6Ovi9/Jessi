# Nexus Halo Firmware v2.3.0 & App v1.2.0

## Firmware Updates (v2.3.0)
- **FreeRTOS Zero-Overhead Deep Sleep**: Replaced all polling `delay()` and bare-metal `__WFE()` calls with proper Adafruit nRF52 FreeRTOS `suspendLoop()` and `resumeLoop()`. This elegantly halts the main Arduino task without starving the RTOS scheduler, preserving the Bluetooth stack thread while completely halting CPU execution for maximum battery savings.
- **State Machine Architecture**: Fixed an infinite ping-pong loop when a mode timed out. Radar and distance modes now cleanly fall back to `STATE_CLOCK_CONNECTED` or `STATE_CLOCK_DISCONNECTED`.
- **Haptic Non-Blocking Refactor**: Removed `delay()` from the haptic controller's `playPattern` method, fully integrating it into the non-blocking `update()` loop so vibrations don't lag the BLE stack.
- **I2C Deadlock Protection**: Fixed a bug where Mbed OS `Wire.end()` workarounds disabled the `NRF_TWIM1` peripheral, which conflicted with the internal `Wire1` IMU bus.
- **LED Clock Face Orientation**: Fixed the mathematical logic inversion so the LEDs correctly rotate clockwise physically instead of counter-clockwise.
- **IMU Calibration Stability**: Adjusted threshold validation logic and fixed multi-tap latency through precise debounce timestamping (`sequence_start_ms`).

## Flutter App Updates (v1.2.0)
- **Optimistic Concurrency & Error Handling**: Refactored `partner_repository.dart` saving logic to properly rethrow exceptions to the UI. The user now receives red snackbar alerts on `settings_screen.dart` if their configuration fails to sync.
- **Real-Time IMU Calibration View**: Implemented the `imu_stream_char` characteristic to stream live `mg` (acceleration) and `dps` (gyro) data from the watch to the app. The `wake_calibration_screen.dart` now actively visualizes real-time movements rather than relying on blunt interrupt events, drastically improving calibration UX.
- **Threshold Limit Bumps**: Increased `wakeThreshold` max to 63, and `gyroThreshold` to 2000 in the UI.
- **App Service Stability**: Fixed isolated bindings initialization to prevent crashes when the foreground service started. Disposed of orphaned animation controllers. Replaced obsolete compass bearing logic.
