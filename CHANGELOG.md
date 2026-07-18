# Changelog

All notable changes to this project will be documented in this file.

## [v2.5 Firmware / v1.4 App] - 2026-07-18

### Added
- **Configurable Distance Gauge LEDs**: Introduced the ability to define exactly how many LEDs correspond to each of the 5 distance zones (Near, Provincial, Far, Very Far, Extreme). This enforces a strict budget of exactly 12 LEDs.
- **Piecewise Linear LED Fill**: Replaced the logarithmic logic with a piecewise linear logic for distance LED mapping, both on the watch firmware and in the app UI watch preview.
- **Supabase Migration**: Added migration script `supabase_migration_leds.sql` to support the new distance zones.

### Fixed
- **Battery Measurement Bug**: Fixed a hardware bug where `VBAT_ENABLE` was never configured as an `OUTPUT` pin. This left the voltage divider floating, causing the ADC to saturate and the battery percentage to permanently display as 100%.
- **Missing State Name Fallback**: Fixed an issue in the serial monitor where Compass Calibration mode would output `Transition to UNKNOWN` due to a missing switch case in `getStateName()`.
- **Clock Desync Drift**: Fixed an overlapping clock hand issue at 12:00 by fixing a UUID collision in the `ble_service.dart` where the Time Sync characteristic was using the wrong UUID (`2a61` instead of `2a2b`).

## [v2.4 Firmware / v1.3 App] - 2026-07-17

### Added
- **Diagnostic Compass Screen**: Real-time live compass stream visualization over BLE to diagnose magnetometer interference.
- **Configurable Triple Flick Window**: Added `tripleFlickWindowMs` to both `WatchConfig` and the App to customize the timeout required for a Triple Flick haptic event.
- **Haptic Testing**: Implemented concurrent execution in the "Probar Vibración" button. It now simultaneously sends the BLE command directly to the watch and the event to Supabase, hiding network latency.

### Fixed
- **Button Spamming**: Added debounce and loading state `_isHapticSending` to prevent database duplicate insertions and API rate limits.
- **Calibration Animation Delay**: Switched the `handleStateCompassCalibration` in the firmware to use a non-blocking `millis()` based state machine instead of hard `delay()`. This prevents the Watchdog Timer from resetting the microcontroller during calibration feedback.
- **Battery Sag (Brownout)**: Modified `animateHapticRX` and `handleStateHapticTX` to use the global configured brightness rather than 255 to prevent voltage drops causing device reboots.

## [v2.3 Firmware / v1.1 App] - 2026-07-06

### Added
- **Complete End-to-End Audit**: Full code-level fixes implemented from deep hardware bug hunt.

### Fixed
- **LED Mirroring Logic**: Removed physical index inversion `(12 - index)` across both `led_controller.cpp` and Flutter `watch_preview_widget.dart` to establish a true native clockwise rotation.
- **IMU & Gestures**: Corrected resolution scale from raw values, added `sequence_start_ms` for robust double-tap detection, prevented float truncation `fabs()`, and added robust data-tearing debounce.
- **Compass I2C Lockup**: Bit-bang 9 clocks on SCL at initialization to recover the bus if the watch was powered down mid-transaction.
- **BLE & State Machine**: Fixed BLE deep sleep connection parameters to respect Apple multiple-of-15ms constraints and 500ms delay. Prevented Timer deadlock in `Calibration Mode`.
- **EEPROM Integrity**: Implemented atomic temporary file rename (`LittleFS.rename()`) after file removal to ensure OTA configurations are safely overwritten.
- **App Foreground Service**: Re-implemented `FlutterForegroundTask` bindings and removed deprecated `foregroundServiceType` references to maintain Android 14+ compatibility.
- **App Location Services**: Migrated Geolocator APIs to fix `locationSettings` depreciation and ensured `distanceKm` calculations no longer throw state errors.
- **App Settings State**: Fixed async `BuildContext` overwrite logic so calibrations save accurately to Supabase.
