# Changelog

All notable changes to this project will be documented in this file.

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
