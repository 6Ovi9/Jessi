# Implementation Plan: Nexus Halo Bug Fixes

> **Pre-condition:** Before any agent touches source files, commit the current state to git:
> ```
> git add -A && git commit -m "chore: checkpoint before bug fix sweep"
> ```
> This gives a clean rollback point for every phase.

---

## Agent Architecture

Each **Coder** agent is fresh (no prior context). It must:
1. Read the implementation plan (this file).
2. Read **only its assigned section** of `C:\Users\ovijo\OneDrive\Desktop\Jessi\fixes_20260716_171234.md`.
3. Read the actual source files it will modify.
4. Apply the fixes.
5. Report back with a summary of every change made (file + line).

Each **Reviewer** agent is fresh. It must:
1. Read the implementation plan.
2. Read the assigned section of the fixes file.
3. Read the **actual modified source files** (post-coder).
4. Report `GREEN` or `RED: <reason>` for each fix it checks.

**Hard rule:** No coder touches files outside its assigned section. No reviewer modifies files.

---

## Phase 1 — Firmware Fixes (Sections 5, 6, 7)

Order: Firmware first. All firmware fixes are independent of each other within the phase, except FW Bug 17 (mutex) which must be done by Coder F3 last so the mutex is initialized before it is used.

### Coder F1 — Power, Haptic (FW Bugs 3, 4)

**Reads:** Fixes file Section 5 (Bugs 3 and 4 only — skip 1 and 2, they are false positives).
**Files to modify:**
- `nexus_halo/power.cpp`
- `nexus_halo/haptic.cpp`

**Tasks:**

#### FW Bug 3 — IMU ODR mismatch (`power.cpp` line 132)
- Change `_writeLSM6DS3Register(0x10, 0x10)` to `_writeLSM6DS3Register(0x10, 0x20)`.
- Update the comment from "12.5 Hz" to "26 Hz" and `ODR_XL=0001` to `ODR_XL=0010`.

#### FW Bug 4 — `stopMotor()` early-return skips PWM zero (`haptic.cpp` lines 70–76)
- Replace the function body so `analogWrite(pin_motor, 0)` and `digitalWrite(pin_motor, LOW)` are **always** called, not guarded by `if (!vibrating)`.
- Correct implementation:
  ```cpp
  void HapticController::stopMotor() {
    analogWrite(pin_motor, 0);
    digitalWrite(pin_motor, LOW);
    vibrating = false;
    pattern_active = false;
  }
  ```

#### False Positives (FW Bugs 1, 2) — verify only
- Read the relevant lines. Leave a `// VERIFIED: no change needed` comment if it adds clarity.

**Report:** List every line changed (file, old content, new content).

---

### Coder F2 — Gestures, Sensors, Config (FW Bugs 6, 7, 8, 9, 10, 11)

**Reads:** Fixes file Section 6 (all 7 bugs; Bug 5 is a no-op, handle it as verify-only).
**Files to modify:**
- `nexus_halo/gesture.cpp`
- `nexus_halo/gesture.h`
- `nexus_halo/compass.cpp`
- `nexus_halo/runtime_config.h`
- `nexus_halo/runtime_config.cpp`

**Tasks:**

#### FW Bug 5 — False positive — verify only
- Search `gesture.cpp`, `imu_calibrator.cpp` for `31.25f`. Confirm it doesn't exist. No code change.

#### FW Bug 6 — `sequence_start_ms` missing from constructor initializer list (`gesture.cpp` lines 3–12)
- **CRITICAL NOTE:** `flick_count(0)` is already at line 6. Do NOT add it again.
- Add ONLY `sequence_start_ms(0)` after `gesture_reported(true)` in the initializer list.
- Final initializer list must be:
  ```cpp
  : imu_ptr(nullptr),
    last_flick_ms(0),
    flick_count(0),        // already present — do not duplicate
    flick_reset(true),
    gyro_threshold(GESTURE_GYRO_THS_DEFAULT),
    double_flick_window(GESTURE_DOUBLE_FLICK_WINDOW_DEFAULT),
    detected_gesture(GESTURE_NONE),
    gesture_reported(true),
    sequence_start_ms(0)   // ← this is the new addition
  ```

#### FW Bug 7 — Static `tear_debounce` survives `reset()` (`gesture.cpp` line 44, `gesture.h`)
- In `gesture.h`: Add `uint8_t tear_debounce;` to the private section of `GestureDetector`.
- In `gesture.cpp` constructor initializer list: Add `tear_debounce(0)` after `sequence_start_ms(0)`.
- In `gesture.cpp` `update()` at line 44: Change `static uint8_t tear_debounce = 0;` to just use the member variable (remove the `static` declaration entirely — the member is now used instead).
- In `gesture.cpp` `reset()`: Add `tear_debounce = 0;` to the reset body.

#### FW Bug 8 — Timeout logic: inverted guard in `update()` (`gesture.cpp` lines 28–32)
- The condition at line 28 is `if (gesture_reported || detected_gesture == GESTURE_NONE)`.
- This is inverted — it sets a new gesture when one is already reported.
- Change to: `if (!gesture_reported || detected_gesture == GESTURE_NONE)`.
  - More precisely: only overwrite detected_gesture if no gesture has been set yet:
  ```cpp
  if (detected_gesture == GESTURE_NONE) {
    if (flick_count == 1) detected_gesture = GESTURE_TAP_SIMPLE;
    else if (flick_count >= 2) detected_gesture = GESTURE_TAP_DOUBLE;
    gesture_reported = false;
  }
  flick_count = 0;
  ```

#### FW Bug 9 — `powerUp()` calls `i2c_stop()` without prior `i2c_start()` (`compass.cpp` line 290)
- Before `i2c_stop();` at line 290, add `i2c_start();`.

#### FW Bug 10 — `RuntimeConfig` struct needs `__attribute__((packed))` (`runtime_config.h` line 16)
- Change `struct RuntimeConfig {` to `struct __attribute__((packed)) RuntimeConfig {`.

#### FW Bug 11 — Migration path `memcpy` overwrites new fields (`runtime_config.cpp` lines 221–225)
- Replace the entire `if (magic_idx > 0) { ... }` block with:
  ```cpp
  if (magic_idx > 0) {
    return false; // Fall back to factory defaults instead of corrupting data
  }
  ```

**Report:** List every line changed.

---

### Reviewer FR1 — Review Phase 1 Firmware (after F1 and F2 complete)

**Reads:** Fixes file Sections 5 and 6. Reads the modified source files.
**Checks:**
- `power.cpp` line ~132: value is `0x20`, comment says 26Hz.
- `haptic.cpp` `stopMotor()`: no early return, both `analogWrite` and `digitalWrite` always called.
- `gesture.cpp` constructor: `sequence_start_ms(0)` present, `flick_count(0)` not duplicated.
- `gesture.h`: `tear_debounce` member present in class.
- `gesture.cpp` `reset()`: `tear_debounce = 0` present.
- `gesture.cpp` `update()`: `static uint8_t tear_debounce` gone, member used instead.
- `gesture.cpp` timeout guard: logic correctly sets gesture only when `detected_gesture == GESTURE_NONE`.
- `compass.cpp` line ~290: `i2c_start()` precedes `i2c_stop()`.
- `runtime_config.h`: struct has `__attribute__((packed))`.
- `runtime_config.cpp`: migration fallback returns `false`.

**Verdict:** `GREEN` or `RED: <specific issue>`.

---

### Reviewer FR1b — Double Check Phase 1 Firmware (after FR1 gives GREEN)

**Reads:** Same as FR1.
**Checks:** Same checks as FR1. Verify the findings of FR1 to ensure no mistakes.
**Verdict:** `GREEN` or `RED: <specific issue>`.

---

### Coder F3 — State Machine, BLE, Volatile (FW Bugs 12, 13, 14, 15, 16, 17, 18, 20)

**Reads:** Fixes file Section 7 (Bugs 12–23; note Bugs 19, 21, 22, 23 are false positives — verify only).
**Files to modify:**
- `nexus_halo/nexus_halo.ino`
- `nexus_halo/ble_handler.h`
- `nexus_halo/ble_handler.cpp`

**Tasks:**

#### FW Bugs 19, 21, 22, 23 — False positives — verify only
- Read the referenced lines. Confirm no changes needed. Optionally leave a brief `// VERIFIED: correct as-is` comment.

#### FW Bugs 12 + 13 — `clearStateChanged()` timing (`nexus_halo.ino` lines 758–770)
- Replace the `last_handled_state` pattern with an explicit `is_entry` latch before the switch:
  ```cpp
  bool is_entry = state_machine.stateChanged();
  state_machine.clearStateChanged();
  switch (state_machine.getCurrentState()) {
    // pass is_entry to each handler
  }
  ```
- Each `handleStateXxx()` call site in the switch must pass `is_entry` as a parameter.
- Each handler function signature must be updated to accept `bool is_entry`.
- Inside each handler that currently calls `state_machine.stateChanged()`, replace that call with the `is_entry` parameter.

#### FW Bug 14 — `ble_connected` global not `volatile` (`nexus_halo.ino` line 73)
- Change `bool ble_connected = false;` to `volatile bool ble_connected = false;`.

#### FW Bug 15 — `ble_connected` class member not `volatile` (`ble_handler.h` line 94)
- Change `bool ble_connected;` to `volatile bool ble_connected;`.

#### FW Bug 16 — BLE callback fields not `volatile` (`ble_handler.h` lines 88–91)
- Add `volatile` to `conn_timestamp`, `active_conn_handle`, and `imu_stream_requested`.

#### FW Bug 17 — `config_json_buf` race in `_onWrite()` (`ble_handler.cpp` lines 280–288)
- **This fix is for `_onWrite()` only — do NOT touch `notifyConfig()` at lines 375–385.**
- Add `SemaphoreHandle_t json_mutex;` to `ble_handler.h` private section.
- In `BLEHandler::begin()` (or at the end of the constructor), add: `json_mutex = xSemaphoreCreateMutex();`
- In `_onWrite()`, replace the `noInterrupts()` / `interrupts()` block with:
  ```cpp
  else if (chr == &config_char) {
    if (len == 0 || !data || len >= sizeof(config_json_buf)) return;
    if (xSemaphoreTake(json_mutex, portMAX_DELAY)) {
      uint16_t copy_len = len < sizeof(config_json_buf) - 1 ? len : sizeof(config_json_buf) - 1;
      memcpy(config_json_buf, data, copy_len);
      config_json_buf[copy_len] = '\0';
      xSemaphoreGive(json_mutex);
    }
    if (callback_config_update) callback_config_update();
  }
  ```
- In the main loop, wherever `ble_handler.getConfigJson()` is read and parsed, wrap the read with the same mutex:
  ```cpp
  if (xSemaphoreTake(ble_handler.json_mutex, portMAX_DELAY)) {
    // parse config_json_buf here
    xSemaphoreGive(ble_handler.json_mutex);
  }
  ```
  (Note: `json_mutex` will need to be made public or accessed via a getter for this.)

#### FW Bug 18 — `WAKING_UP` transitions unconditionally every tick (`nexus_halo.ino`)
- In `handleStateWakingUp()`, the transition to `STATE_CLOCK_CONNECTED/DISCONNECTED` happens unconditionally every tick (lines 992–996). Add a timer guard using `is_entry`:
  ```cpp
  void handleStateWakingUp(bool is_entry) {
    if (is_entry) {
      // ... existing entry code (power on LED, set advertising, etc.) ...
      state_machine.resetTimer(1500); // 1.5s wake animation
    }
    if (state_machine.isTimerExpired()) {
      if (haptic_rx_pending) {
        haptic_rx_pending = false;
        state_machine.transitionTo(STATE_HAPTIC_RX);
      } else if (ble_handler.isConnected()) {
        state_machine.transitionTo(STATE_CLOCK_CONNECTED);
      } else {
        state_machine.transitionTo(STATE_CLOCK_DISCONNECTED);
      }
    }
  }
  ```

#### FW Bug 20 — `STATE_COMPASS_CALIBRATION` has no timeout (`nexus_halo.ino` lines 1771–1780)
- In `handleStateCompassCalibration(bool is_entry)`, add a 15-second timeout:
  ```cpp
  void handleStateCompassCalibration(bool is_entry) {
    if (is_entry) state_machine.resetTimer(15000);
    if (!compass.isCalibrating() || state_machine.isTimerExpired()) {
      state_machine.transitionTo(ble_handler.isConnected() ? STATE_CLOCK_CONNECTED : STATE_CLOCK_DISCONNECTED);
      return;
    }
    // ... existing LED animation ...
  }
  ```
- **Note:** The original code uses `return`. Since `handleStateCompassCalibration` is a separate function called from the `switch` block, `return` is correct here (it returns from the function back to the switch, then the switch falls through to the bottom of `loop()` which is correct). Do NOT change `return` to `break` unless the code is inlined in the switch.

**Report:** List every line changed.

---

### Reviewer FR2 — Review Phase 1 Firmware (after F3 completes)

**Reads:** Fixes file Section 7. Reads the modified source files.
**Checks:**
- `nexus_halo.ino`: `is_entry` latched before switch, `clearStateChanged()` called before switch.
- `nexus_halo.ino` line ~73: `volatile bool ble_connected`.
- `ble_handler.h` line ~94: `volatile bool ble_connected`.
- `ble_handler.h` lines ~88–91: `volatile` on `conn_timestamp`, `active_conn_handle`, `imu_stream_requested`.
- `ble_handler.h`: `SemaphoreHandle_t json_mutex` declared.
- `ble_handler.cpp` `_onWrite()`: `noInterrupts/interrupts` gone; mutex wraps memcpy; null + length guards present.
- `ble_handler.cpp` `notifyConfig()`: **untouched** — confirm it still has its existing `if (!json) return` and no mutex was added.
- `nexus_halo.ino` `handleStateWakingUp()`: entry guard + `resetTimer(1500)` present; transitions inside timer check.
- `nexus_halo.ino` `handleStateCompassCalibration()`: `resetTimer(15000)` on entry; `isTimerExpired()` check present.

**Verdict:** `GREEN` or `RED: <specific issue>`.

---

### Reviewer FR2b — Double Check Phase 1 Firmware (after FR2 gives GREEN)

**Reads:** Same as FR2.
**Checks:** Same checks as FR2. Verify the findings of FR2 to ensure no mistakes.
**Verdict:** `GREEN` or `RED: <specific issue>`.

---

## Phase 2 — Flutter Fixes (Sections 2, 3, 4)

Starts only after Reviewer FR2 gives `GREEN`. Flutter fixes are grouped by file/subsystem.

### Coder FL1 — Screens (Flutter Bugs 1, 2, 3, 4, 5, 6)

**Reads:** Fixes file Section 2 (all 6 bugs).
**Files to modify:**
- `app/lib/screens/settings_screen.dart`
- `app/lib/screens/wake_calibration_screen.dart`
- `app/lib/screens/wrist_flick_calibration_screen.dart`

**Tasks:**

#### Flutter Bugs 1, 2, 3 — Delete dead `_startCalibration` method (`settings_screen.dart` lines 886–983)
- Delete the entire `_startCalibration` method (lines 886–983).
- This co-resolves all three bugs.

#### Flutter Bug 4 — DB write order + `mounted` guard (`wake_calibration_screen.dart` lines 136–158)
- Rewrite `_writeThreshold()` to: (1) write DB first, (2) write BLE second, (3) rollback DB if BLE fails, (4) add `if (!mounted) return` after every `await`.
- See fixes file for the exact code block.

#### Flutter Bug 5 — Slider writes on every drag (`wrist_flick_calibration_screen.dart` lines 267–273 and 346–353)
- For both sliders, move the `_writeThreshold` / `_writeDoubleFlickWindow` call from `onChanged` to `onChangeEnd`. `onChanged` should only call `setState()` to update the local variable for visual feedback.
- Example:
  ```dart
  Slider(
    value: _threshold.toDouble(),
    onChanged: (v) => setState(() => _threshold = v.round()),
    onChangeEnd: (v) async => await _writeThreshold(v.round()),
  )
  ```

#### Flutter Bug 6 — Role swap with uninitialized repo (`settings_screen.dart` line 1015)
- Before `await prefs.setString('user_role', repo.partnerUserId)`, add:
  ```dart
  if (repo.partnerUserId.isEmpty) return;
  ```

**Report:** List every line changed.

---

### Coder FL2 — BLE Service (Flutter Bugs 7, 8, 9, 10, 11, 12, 13, 14)

**Reads:** Fixes file Section 3, bugs 7–14 only.
**Files to modify:**
- `app/lib/services/ble_service.dart`

**Tasks:**

#### Flutter Bug 7 — Write queue deadlock (`ble_service.dart` lines 568–578)
- Replace `_bleWriteQueue.whenComplete(...)` with `catchError` + `then` + 5-second timeout:
  ```dart
  Future<void> _enqueueWrite(Future<void> Function() writeOp) {
    _bleWriteQueue = _bleWriteQueue.catchError((_) {}).then((_) async {
      try {
        await writeOp().timeout(const Duration(seconds: 5));
      } catch (e) {
        print('[BLE] Write error/timeout: $e');
      }
    });
    return _bleWriteQueue;
  }
  ```

#### Flutter Bug 8 — `_onConnected` is `void async` (`ble_service.dart` line 418)
- Change `void _onConnected(String deviceId) async {` to `Future<void> _onConnected(String deviceId) async {`.

#### Flutter Bug 9 — `cancelCalibration` kills subscription permanently (`ble_service.dart` line 839)
- After `_calibStatusSubscription?.cancel();`, add `_calibStatusSubscription = null;`.
- In `startCalibration()`, before `sendCalibCmd(0x01)`, add:
  ```dart
  _subscribeToCalibStatus(_connectedDeviceId!);
  ```

#### Flutter Bug 10 — `sendCalibCmd` missing connection state check (`ble_service.dart` line 800)
- Add `_connectionState != BleConnectionState.connected ||` to the existing null guard:
  ```dart
  if (_connectionState != BleConnectionState.connected || _connectedDeviceId == null) return;
  ```

#### Flutter Bug 11 — Calibration unawaited + no timeout (`ble_service.dart` lines 827 and 850)
- In `startCalibration()`, change `sendCalibCmd(0x01);` to `await sendCalibCmd(0x01).timeout(const Duration(seconds: 15));`.
- In `startCompassCalibration()`, change `sendCalibCmd(0x04);` to `await sendCalibCmd(0x04).timeout(const Duration(seconds: 15));`.
- Wrap both in try/catch to handle `TimeoutException` gracefully.

#### Flutter Bug 12 — Double reconnect timer (`ble_service.dart` line 376)
- Before `_retryTimer?.cancel(); _retryTimer = Timer(...)`, add:
  ```dart
  if (_retryTimer?.isActive ?? false) return;
  ```
  (Note: The existing `.cancel()` call can stay for safety but the early return prevents duplicate scheduling.)

#### Flutter Bug 13 — Stale subscriptions on reconnect (`ble_service.dart` lines 440–450)
- Add a `_cancelAllSubscriptions()` private method:
  ```dart
  void _cancelAllSubscriptions() {
    _hapticTxSubscription?.cancel(); _hapticTxSubscription = null;
    _batterySubscription?.cancel(); _batterySubscription = null;
    _calibStatusSubscription?.cancel(); _calibStatusSubscription = null;
    _calibThresholdSubscription?.cancel(); _calibThresholdSubscription = null;
    _radarModeSubscription?.cancel(); _radarModeSubscription = null;
    _imuStreamSubscription?.cancel(); _imuStreamSubscription = null;
  }
  ```
- Call `_cancelAllSubscriptions()` at the very start of `_onConnected()`.

#### Flutter Bug 14 — Y2038 Unix epoch truncation (`ble_service.dart` line 603)
- This requires a firmware protocol change to support 64-bit timestamps. **Do not change the Dart code yet.** Instead, add a `TODO` comment:
  ```dart
  // TODO(Y2038): nowEpoch is truncated to uint32. Firmware must be updated
  // to accept a uint64 timestamp before this can be fixed safely.
  bytes.setUint32(0, nowEpoch, Endian.little);
  ```

**Report:** List every line changed.

---

### Coder FL3 — BLE Service continued + Location + Foreground (Flutter Bugs 15, 17, 18, 19, 27)

**Reads:** Fixes file Section 3, bugs 15–19. Also Bug 27 from Section 4 (it's in `sync_service.dart`).
**Files to modify:**
- `app/lib/services/foreground_service.dart`
- `app/lib/services/location_service.dart`
- `app/lib/services/sync_service.dart`

**Tasks:**

#### Flutter Bug 15 — `start()` always returns `true` (`foreground_service.dart` line 75)
- Change the second `return true;` (line 75, which follows the `startService` call) to `return result;`.

#### Flutter Bug 16 — False positive — verify only
- Read line 133 (`WidgetsFlutterBinding.ensureInitialized()`). Confirm it should stay. No change.

#### Flutter Bug 17 — `_isRunning` not rechecked after async GPS call (`location_service.dart` line 284)
- After `_currentPosition = position;` (line 284), add:
  ```dart
  if (!_isRunning) return;
  ```

#### Flutter Bug 18 — `_doGpsUpdate()` unawaited in `setRadarModeActive` (`location_service.dart` line 214)
- Change `_doGpsUpdate();` to `await _doGpsUpdate();`.
- Change `void setRadarModeActive(bool active)` signature to `Future<void> setRadarModeActive(bool active) async`.
- Update call sites of `setRadarModeActive` to `await` it (check `home_screen.dart` and anywhere else it is called).

#### Flutter Bug 19 — Android 11+ "always" location permission silently fails (`location_service.dart` line 107)
- Replace the `Geolocator.requestPermission()` call in the `shouldUpgradeToAlwaysPermission` branch with:
  ```dart
  await Geolocator.openAppSettings();
  // After returning from settings, re-check the permission
  final upgraded = await Geolocator.checkPermission();
  if (upgraded != LocationPermission.denied && upgraded != LocationPermission.deniedForever) {
    permission = upgraded;
  }
  ```

#### Flutter Bug 27 — Duplicate haptic events (`sync_service.dart` lines 209–223)
- Add two fields to `SyncService`:
  ```dart
  final _processedHapticIds = <String>{};
  static const _kMaxProcessedIds = 50;
  ```
- In the Realtime callback at line ~214 (before `_consumeHapticEvent(eventId)`), add:
  ```dart
  if (_processedHapticIds.contains(eventId)) return;
  if (_processedHapticIds.length >= _kMaxProcessedIds) {
    _processedHapticIds.remove(_processedHapticIds.first);
  }
  _processedHapticIds.add(eventId);
  ```

**Report:** List every line changed.

---

### Reviewer FLR1 — Review Phase 2 Coders FL1, FL2, FL3

**Reads:** Fixes file Sections 2 and 3 (all bugs). Reads modified source files.
**Checks:**
- `settings_screen.dart`: `_startCalibration` entirely gone. Role swap has `isEmpty` guard.
- `wake_calibration_screen.dart`: DB written first, rollback on BLE fail, `mounted` checks after every `await`.
- `wrist_flick_calibration_screen.dart`: Both sliders use `onChangeEnd`, not `onChanged`, for network writes.
- `ble_service.dart`: Write queue uses `catchError` + `then` + 5s timeout. `_onConnected` returns `Future<void>`. `cancelCalibration` sets subscription to `null`. `sendCalibCmd` checks `_connectionState`. Both calib commands are awaited with 15s timeout. Reconnect timer has early return guard. `_cancelAllSubscriptions` called at start of `_onConnected`. Y2038 has `TODO` comment.
- `foreground_service.dart`: Returns `result` not hardcoded `true`.
- `location_service.dart`: `_isRunning` check after GPS await. `setRadarModeActive` is async. Android 11 uses `openAppSettings`.
- `sync_service.dart`: `_processedHapticIds` Set + bounded eviction logic in callback.

**Run:** `flutter analyze app/` and paste the output. Report any new errors introduced by these changes.

**Verdict:** `GREEN` or `RED: <specific issue>`.

---

### Coder FL4 — Core / Main (Flutter Bugs 20, 21, 22, 23, 24, 25, 26)

**Starts only after FLR1 gives GREEN.**
**Reads:** Fixes file Section 4, bugs 20–26.
**Files to modify:**
- `app/lib/main.dart`
- `app/lib/services/sync_service.dart`

**Tasks:**

#### Flutter Bugs 20 + 21 — Hardcoded URL + key (`main.dart` lines 24, 31)
- Add `flutter_dotenv` to `pubspec.yaml` (`flutter pub add flutter_dotenv`).
- Create `app/.env` (and add it to `.gitignore`):
  ```
  SUPABASE_URL=http://100.103.87.29:8000
  SUPABASE_ANON_KEY=eyJhbGci...
  ```
- In `main.dart`, load `.env` before `Supabase.initialize`:
  ```dart
  await dotenv.load(fileName: '.env');
  ```
- Replace the hardcoded constants with:
  ```dart
  final supabaseUrl = dotenv.env['SUPABASE_URL']!;
  final supabaseAnonKey = dotenv.env['SUPABASE_ANON_KEY']!;
  ```

#### Flutter Bug 22 — `late _client` use-before-init (`sync_service.dart` line 18)
- Remove the `late SupabaseClient _client;` field declaration.
- In every method that uses `_client`, replace it with `Supabase.instance.client`:
  - `checkConnection()` → `Supabase.instance.client.from(...)`
  - `uploadLocation()` → `Supabase.instance.client.from(...)`
  - `_subscribeToPartnerLocation()` → `Supabase.instance.client.channel(...)`
  - `_subscribeToHapticEvents()` → same
  - `sendHapticEvent()` → same
  - `_consumeHapticEvent()` → same
  - `fetchPartnerLocation()` → same
  - `cleanupOldHapticEvents()` → same

#### Flutter Bug 23 — Null dereference on `_selectedUserRole!` (`main.dart` line 160)
- In `_updateLocationIntervals()`, add at the top:
  ```dart
  if (_selectedUserRole == null) return;
  ```

#### Flutter Bug 24 — Re-entrant `_bootstrap()` (`main.dart` line 170)
- Add a `Future<void>? _bootstrapFuture;` field to `_AppBootstrapperState`.
- Replace the `_bootstrap()` method body:
  ```dart
  Future<void> _bootstrap() {
    if (_bootstrapFuture != null) return _bootstrapFuture!;
    _bootstrapFuture = _doBootstrap().catchError((e) {
      _bootstrapFuture = null;
      throw e;
    });
    return _bootstrapFuture!;
  }
  ```
- Rename the existing `_bootstrap()` body to `_doBootstrap()`.

#### Flutter Bug 25 — Missing `mounted` checks in `_doBootstrap()` (`main.dart` lines 201–209)
- After every `await` in `_doBootstrap()`, add `if (!mounted) return;` before any `setState()` call or context interaction.

#### Flutter Bug 26 — Misleading indentation (`sync_service.dart` lines 141–183)
- Re-indent `_subscribeToPartnerLocation()` so the outer `try` block correctly encloses the `_locationChannel = ...` assignment. The `_locationChannel` should be inside the `try`, not accidentally in the `finally` indentation gap.

**Report:** List every line changed.

---

### Flutter Bug 28 + 31 — Coder FL5 (Models + Bearing)

**Reads:** Fixes file — Bug 28 (Section 3, false positive), Bug 29 (already fixed), Bug 30 and Bug 31 (Section 4).
**Files to modify:**
- `app/lib/services/bearing_calculator.dart`
- `app/lib/models/location_model.dart`

**Tasks:**

#### Flutter Bug 28 — False positive — verify only
- Read `sync_service.dart` reconnect logic. Confirm Supabase handles reconnects natively. No change.

#### Flutter Bug 29 — Already fixed — verify only
- Read `bearing_calculator.dart` line 108. Confirm `% 12` is already present. No change.

#### Flutter Bug 30 — Dead `_earthRadiusKm` constant (`bearing_calculator.dart` line 12)
- Remove the line `static const double _earthRadiusKm = 6371.0;`.

#### Flutter Bug 31 — `isValid` rejects (0,0) (`location_model.dart` line 51)
- Change:
  ```dart
  bool get isValid => latitude != 0 || longitude != 0;
  ```
  to:
  ```dart
  bool get isValid =>
      latitude >= -90 && latitude <= 90 &&
      longitude >= -180 && longitude <= 180;
  ```

**Report:** List every line changed.

---

### Reviewer FLR2 — Final Flutter Review (after FL4 and FL5 complete)

**Reads:** Fixes file Section 4 (all bugs). Reads modified source files.
**Checks:**
- `main.dart`: No hardcoded URL or key. `dotenv` used. `_selectedUserRole` null guard in listener. `_bootstrap` caches Future with error reset. `_doBootstrap` has `mounted` checks after each `await`.
- `sync_service.dart`: No `late _client` field. All methods use `Supabase.instance.client`. Indentation in `_subscribeToPartnerLocation` is correct.
- `bearing_calculator.dart`: `_earthRadiusKm` removed. `bearingToLedIndex` still has `% 12` (untouched).
- `location_model.dart`: `isValid` uses bounds check instead of `!= 0`.

**Run:** `flutter analyze app/` and paste the output. Report any new errors.

**Verdict:** `GREEN` or `RED: <specific issue>`.

---

## Phase 3 — Final Integration Check

After both FLR2 gives `GREEN`:

1. Orchestrator builds the Flutter app: `cd app && flutter build apk --debug` and moves the resulting APK to the root directory `c:\Users\ovijo\OneDrive\Desktop\Jessi`.
2. Confirm firmware compiles in Arduino IDE (manual step — user does this).
3. `git add -A && git commit -m "fix: apply all 48 high-priority bug fixes"`.

---

## Coder Prompt Template

When invoking any Coder agent, use this prompt structure:

```
You are [Coder F1 / FL2 / etc.]. Read the implementation plan at:
C:\Users\ovijo\.gemini\antigravity\brain\f131dc1a-bdd1-4115-a455-1a0a01b6f446\implementation_plan.md

Then read ONLY your assigned section of the fixes file at:
C:\Users\ovijo\OneDrive\Desktop\Jessi\fixes_20260716_171234.md

Then read the actual source files listed in your section.
Apply the fixes exactly as described. Do NOT touch files outside your section.
When done, report every file and line you changed (old value → new value).
```

## Reviewer Prompt Template

```
You are [Reviewer FR1 / FLR2 / etc.]. Read the implementation plan at:
C:\Users\ovijo\.gemini\antigravity\brain\f131dc1a-bdd1-4115-a455-1a0a01b6f446\implementation_plan.md

Then read your assigned section of the fixes file.
Then read the actual modified source files.
Check each item in your checklist.
Do NOT modify any files.
Report GREEN or RED: <specific issue> for each item.
```

---

## Summary Table

| Agent | Phase | Bugs | Files |
|---|---|---|---|
| Coder F1 | Firmware | FW 3, 4 | `power.cpp`, `haptic.cpp` |
| Coder F2 | Firmware | FW 5(verify), 6, 7, 8, 9, 10, 11 | `gesture.cpp`, `gesture.h`, `compass.cpp`, `runtime_config.h`, `runtime_config.cpp` |
| Reviewer FR1 | Firmware | Review F1+F2 | All above |
| Reviewer FR1b | Firmware | Double Check F1+F2 | All above |
| Coder F3 | Firmware | FW 12, 13, 14, 15, 16, 17, 18, 20 | `nexus_halo.ino`, `ble_handler.h`, `ble_handler.cpp` |
| Reviewer FR2 | Firmware | Review F3 | All above |
| Reviewer FR2b | Firmware | Double Check F3 | All above |
| Coder FL1 | Flutter | FL 1, 2, 3, 4, 5, 6 | `settings_screen.dart`, `wake_calibration_screen.dart`, `wrist_flick_calibration_screen.dart` |
| Coder FL2 | Flutter | FL 7, 8, 9, 10, 11, 12, 13, 14 | `ble_service.dart` |
| Coder FL3 | Flutter | FL 15, 16(verify), 17, 18, 19, 27 | `foreground_service.dart`, `location_service.dart`, `sync_service.dart` |
| Reviewer FLR1 | Flutter | Review FL1+FL2+FL3 | All above |
| Coder FL4 | Flutter | FL 20, 21, 22, 23, 24, 25, 26 | `main.dart`, `sync_service.dart` |
| Coder FL5 | Flutter | FL 28(verify), 29(verify), 30, 31 | `bearing_calculator.dart`, `location_model.dart` |
| Reviewer FLR2 | Flutter | Review FL4+FL5 | All above |

**Total: 12 agents. 4 reviewers. No single agent touches more than 5 files.**
