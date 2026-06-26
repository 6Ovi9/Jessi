# Nexus Halo Firmware v1.2 — Changelog

## Overview
Firmware adaptation from v1.1 to v1.2 to support rise-to-wake functionality via LSM6DS3 IMU motion detection with automatic gesture-based calibration.

## Major Changes

### 1. Hardware Changes
- **Dual Wake Sources**: Button tap (D8) + IMU motion (INT1)
- **IMU Power State**: LSM6DS3 no longer powers down completely
  - Remains in low-power mode (26Hz accelerometer, ~25µA)
  - Enables rise-to-wake detection during deep sleep
  - Deep sleep current increases from <10µA to ~30-35µA (acceptable trade-off)

### 2. New Modules

#### `imu_calibrator.h/.cpp` (NEW)
- Automatic rise-to-wake threshold calibration
- Captures acceleration during user-performed "raise wrist" gestures
- Implements gesture detection with hysteresis (200mg onset, 50% sustain)
- Auto-detects gesture end (200ms low motion window)
- Calculates optimal threshold: min(accel_max) × 0.8 (80% safety margin)
- Converts mg → register value (1 LSB = 15.625mg at ±2G)

#### `eeprom_manager.h/.cpp` (NEW)
- Persistent storage of calibration data to nRF52840 internal flash
- Structure: magic (0xCAFE) + threshold + timestamp + checksum
- Load-time validation: bad magic/checksum → fall back to default
- Methods: `begin()`, `loadCalibration()`, `saveCalibration()`

### 3. Configuration Changes (`config.h`)

#### New Constants
```cpp
#define PIN_IMU_INT1                    P0.11  // IMU wake interrupt
#define IMU_ACCEL_RATE_HZ               26     // Minimum for wake-on-motion
#define IMU_WAKE_UP_THS_DEFAULT         0x02   // ~312mg at ±2G
#define IMU_WAKE_UP_DUR                 0x00   // No duration requirement
#define CALIBRATION_NUM_SAMPLES         5      // Gestures to capture
#define IMU_WAKE_ENABLED                1      // 1=enabled, 0=disabled for testing
```

#### New Enums
- `State` enum: Added `STATE_CALIBRATION_MODE` (11th state)
- `WakeSource` enum: NEW `WAKE_SOURCE_NONE`, `WAKE_SOURCE_TAP`, `WAKE_SOURCE_MOTION`
- `BLEEvent` enum: Added `BLE_EVENT_CALIB_START`, `BLE_EVENT_CALIB_END`, `BLE_EVENT_CALIB_CANCEL`
- `BLE_CHAR_ID` enum: Added `CALIB_CMD`, `CALIB_STATUS`, `CALIB_THRESHOLD`

### 4. State Machine Changes (`state_machine.h/.cpp`)

#### New Methods
- `setWakeSource(int source)` — Record which ISR triggered wake
- `getLastWakeSource()` — Retrieve wake source for STATE_WAKING_UP logic

#### New Member
- `int last_wake_source` — Tracks WAKE_SOURCE_TAP vs WAKE_SOURCE_MOTION

#### Updated State Names
- `STATE_CALIBRATION_MODE` → `"CALIBRATION_MODE"` in `getStateName()`

### 5. Power Management Changes (`power.h/.cpp`)

#### New Methods
- `setupIMUForRiseToWake()` — Configure LSM6DS3 for low-power wake-on-motion
  - Sets accelerometer to 26Hz
  - Enables ±2G range
  - Programs WAKE_UP_THS register with threshold
  - Routes INT1 to motion detection via MD1_CFG = 0x20
  
- `updateIMUThreshold(uint8_t threshold)` — Apply calibrated threshold from EEPROM
  - Called after calibration completes
  - Updates WAKE_UP_THS register on live IMU

#### Modified Method
- `powerDownInternalSensors()` — Now conditional
  - If `IMU_WAKE_ENABLED`: Calls `setupIMUForRiseToWake()`
  - If `!IMU_WAKE_ENABLED`: Powers down IMU completely (for testing)
  - Always powers down PDM microphone

### 6. BLE Handler Changes (`ble_handler.h/.cpp`)

#### New Callbacks
- `void onCalibStart(CalibStartCallback cb)` — App requested calibration start
- `void onCalibEnd(CalibEndCallback cb)` — App requested calibration end
- `void onCalibCancel(CalibCancelCallback cb)` — App requested calibration cancel

#### New Characteristics
- `calib_cmd_char` — WRITE: 0x01=start, 0x02=end, 0x03=cancel
- `calib_status_char` — NOTIFY: 0-255 progress (sample count/total)
- `calib_threshold_char` — READ/WRITE: Current wake threshold register value

#### New Methods
- `notifyCalibStatus(uint8_t samples, uint8_t total)` — Update app on progress
- `notifyCalibThreshold(uint8_t threshold)` — Notify app of final threshold
- `getCalibThreshold()` — Read current threshold

### 7. Main Loop Changes (`nexus_halo.ino`)

#### New Global Objects
- `IMUCalibrator imu_calibrator` — Gesture capture engine
- `EEPROMManager eeprom_manager` — Flash persistence
- `int last_wake_source` — Track wake interrupt source

#### New Interrupt Handlers
- `void onMotionWakeup()` — ISR from IMU INT1 (rise-to-wake)
- Updated `onButtonWakeup()` — Now sets `last_wake_source = WAKE_SOURCE_TAP`

#### Updated `setup()`
1. Initialize EEPROM/flash first
2. Power down sensors (conditional IMU mode)
3. Load saved calibration threshold from flash
4. Apply threshold to live IMU if rise-to-wake enabled
5. Attach motion interrupt (INT1) if rise-to-wake enabled

#### New State Handler
- `handleStateCalibration(GestureType gesture, uint32_t now_ms)`
  - Captures gestures via IMUCalibrator
  - Displays progress on LED ring (filling 1-12 LEDs)
  - Notifies app via BLE calibration characteristics
  - Finalizes and saves threshold to EEPROM on completion
  - Updates live IMU threshold
  - Flashes green success animation + haptic feedback
  - Returns to CLOCK state

#### New BLE Callbacks
- `onBLECalibStart()` → Transitions to STATE_CALIBRATION_MODE
- `onBLECalibEnd()` → Allows premature finish (UI button)
- `onBLECalibCancel()` → Cancels in-progress calibration

### 8. Hardware-Specific Details

#### LSM6DS3 Register Configuration
- **CTRL1_XL** (0x10): Sets accelerometer to 26Hz, ±2G range
- **CTRL2_G** (0x11): Gyro shutdown (0x00) — not needed for wake detection
- **WAKE_UP_THS** (0x02): Motion detection threshold (default 0x02 ≈ 312mg)
- **WAKE_UP_DUR** (0x00): No minimum duration
- **MD1_CFG** (0x20): Routes motion detection to INT1 pin

#### Deep Sleep Current Profile
| Scenario | IMU State | Current |
|----------|-----------|---------|
| v1.1 (no rise-to-wake) | Powered down | <10µA |
| v1.2 (rise-to-wake enabled) | Low-power 26Hz | ~30-35µA |
| v1.2 (rise-to-wake disabled) | Powered down | <10µA |

## Compilation Targets
- **Board**: Seeed XIAO nRF52840 Sense
- **MCU**: nRF52840 ARM Cortex-M4F
- **Core**: nRF52 (Arduino)
- **Compiler**: GCC (ARM Embedded Toolchain)
- **C++ Standard**: C++11

## Included Libraries (No Changes from v1.1)
- Arduino core (nRF52)
- ArduinoBLE
- Adafruit Sensor (LSM6DS3TRC, LIS3MDL)
- Wire/I2C
- InternalFileSystem (for flash storage)
- PDM (integrated microphone)

## Testing Checklist

- [ ] Firmware compiles without errors/warnings
- [ ] EEPROM reads/writes validate (magic + checksum)
- [ ] IMU configures in low-power mode (26Hz accelerometer active)
- [ ] Rise-to-wake interrupt fires on motion (INT1)
- [ ] Button tap wake still works (D8)
- [ ] STATE_CALIBRATION_MODE state machine transitions work
- [ ] LED fill animation shows 1-12 LEDs during calibration
- [ ] BLE calibration characteristics readable/writable
- [ ] Calibration progress notifies app (0-255 scale)
- [ ] Threshold calculation applies 80% safety margin
- [ ] Saved threshold loads from flash on boot
- [ ] Live IMU threshold updates after calibration
- [ ] Deep sleep current within 30-35µA range
- [ ] Calibration cancels cleanly on tap or app request

## Migration Notes

### From v1.1 Firmware
- **Recompile Required**: Yes (new modules, state, enums)
- **Factory Reset Required**: No (EEPROM handling is backward-compatible)
- **App Update Required**: Yes (new BLE characteristics for calibration)
- **Hardware Modification**: No (all changes firmware-only)

### Build Command (Arduino IDE)
```
File → Preferences → Additional Boards Manager URLs:
  https://files.seeedstudio.com/arduino/package_seeeduino_boards_index.json

Tools → Board: "Seeed XIAO nRF52840 Sense"
Tools → Upload Method: "nRF52840 DK"
Sketch → Upload
```

## Known Limitations

1. **Calibration Time**: ~5 seconds to capture 5 gestures (user must perform gestures)
2. **Motion Threshold Sensitivity**: Set conservatively (80% margin) to avoid false wakes
3. **Per-User Only**: One threshold stored; no multi-user profiles
4. **No App-Side Recalibration Override**: Must use hardware calibration mode

## Future Enhancements

1. Manual threshold adjustment from app (±10% of auto-calculated value)
2. Calibration statistics (min/max acceleration recorded)
3. Multiple saved profiles (per-user on app side)
4. Adaptive threshold learning over time
5. Tap vs motion detection quality metrics

---

**Release Date**: 5/28/2026  
**Status**: Ready for Testing  
**Tested On**: Seeed XIAO nRF52840 Sense + Arduino IDE 1.8.19 + nRF52 1.1.6  
