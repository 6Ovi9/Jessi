# Changelog

All notable changes to this project will be documented in this file.

## [v2.1.0] - 2026-07-05 (The Hardware Precision Update)
### Fixed & Improved
- **Hardware EasyDMA LED Driver**: Ripped out the software-based `Adafruit_NeoPixel` and implemented a custom `NRF_PWM0` hardware DMA driver for the SK6812 LEDs, definitively solving BLE-induced flashing, color blasts, and NeoPixel corruption.
- **I2C Compass Latency**: Replaced blocked 100kHz standard `Wire` library with a precision 10µs bit-bang implementation, unlocking a 50kHz clock rate for the LIS3MDL without locking up the hardware.
- **Compass Real-time Tuning**: Increased hardware ODR to 80Hz and software polling to 30Hz, combined with an optimized EMA filter, making the radar mode track instantly with wrist rotation.
- **Atomic Time Synchronization**: Overhauled the Dart-to-C++ time synchronization mechanism using an 8-byte payload (Unix Epoch + Timezone Offset). Time variables are now guarded by `noInterrupts()` to prevent volatile tearing, and the app uses exponential backoff to guarantee delivery.
- **Memory Checksum Bug**: Reordered `RuntimeConfig` and `FlashData` to entirely eradicate compiler-injected padding bytes. Implemented zero-initialization explicitly via `memset` across the board, fixing the "reverts to defaults on boot" bug.
- **Overnight Battery Drain**: Internal active-LOW status LEDs (`LED_RED`, `LED_GREEN`, `LED_BLUE`) are now explicitly driven `HIGH` in `setup()`, plugging the massive battery leak.
- **Timer Underflow Fix**: Refreshed `now_ms = millis()` synchronously at the top of the state machine update loop, preventing the watch from instantly falling back to deep sleep upon waking.

## [v2.0.0] - 2026-07-04
### Added
- **Dynamic Location Polling Fallback:** Added a reactive listener in the `AppBootstrapper` to ensure GPS polling intervals automatically apply even if the database connects late due to a slow internet connection.
- **Radar Mode Watch Synchronization:** Added `_subscribeToRadarMode` in `BleService` so the app successfully subscribes to the `RADAR_MODE_CHAR` (0x2A5F). The watch now actively keeps the phone awake when entering Radar Mode, and vice versa.

### Fixed
- **Optimistic UI Clobbering:** Added a `_requestNonce` in `PartnerRepository` to prevent out-of-order network responses from ruining slider inputs and settings updates.
- **Optimistic State Overwrite:** Guarded `loadConfig` against overwriting in-flight UI changes during background synchronization.
- **BLE Scan Memory Leak (Async Gap):** Eliminated a critical async gap in `startScan` by locking `_isScanning` synchronously before awaiting OS permissions. This prevents rapid double-taps from leaking BLE scan streams forever.
- **Zombie Pairing Bug (Persistence Race):** Made `clearPairing` asynchronous and added a `!_isPaired` abort check to `saveDeviceId`. This prevents a canceled pairing from being resurrected from RAM before it was wiped from the disk.
- **Firmware Time Display Bug:** Fixed the Mbed OS bug causing `STATE_CLOCK_CONNECTED` to display corrupted digits by enforcing single-layer frame buffer flushing instead of alternating dual buffers.
- **SysTick Deep Sleep Power Drain:** Refactored `loop()` to bypass the 10ms throttle completely when in `STATE_DEEP_SLEEP`. The RTOS no longer busy-spins when processing BLE background events.
- **NeoPixel Parasitic Leakage:** Converted the LED data pin (`D7`) to `INPUT_PULLDOWN` before cutting D10 power. This prevents the MCU from leaking 15mA through the NeoPixel's internal ESD diodes.
- **PDM Microphone Clock Leak:** Manually overrode the `NRF_P0->PIN_CNF` registers to disconnect the PDM CLK and DIN pins, saving 1mA during sensor shutdown.
- **Compass I2C Sweat Leakage:** Disabled the Bit-Bang I2C pull-ups (13kΩ) on D4 and D5 directly via `PIN_CNF` registers in `compass.powerDown()`, preventing 0.5mA leaks from sweat/moisture bridging the exposed pins.

## [v1.2.0]
### Added
- Rise-to-Wake via LSM6DS3TR-C (ultra-low power 26Hz mode).
- Adaptive Calibration for wrist flicks.
- Double-flick gesture recognition (Haptic TX).
- ... (and prior features)
