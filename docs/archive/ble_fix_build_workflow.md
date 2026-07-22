# Multi-Agent Build & Review Workflow
### BLE Architecture Fix — for Antigravity (Gemini)

This reuses the roles and format from the original migration workflow, with two
process corrections baked in from what actually went wrong last time, plus
checklists specific to this fix (contract expansion, OTA handoff, IMU/compass
throttling, reconnect resubscription).

## Note to the orchestrating agent reading this

Your job is to spawn, sequence, and route work between subagents — not to
write the code yourself. Two rules are non-negotiable this time, because both
were broken last round without anyone flagging it until a human caught it:

1. **No file skips Task Review or File Review, ever — including the last file
   in a batch, and including files that "just need a small change."** Last
   time, `ble_service.dart` shipped with zero review and turned out to contain
   the exact dual-BLE-instance bug this whole fix exists to resolve. If you
   find yourself tempted to skip a review step because things are "basically
   done," that is precisely the moment not to.
2. **When you report status back to the human, include the reviewer's actual
   verdict text (citations, file:line references, PASS/FAIL per item) — not
   your own paraphrase of it.** A paraphrased "reviewer said PASS" cannot be
   checked by anyone reading your summary. Quote the reviewer.

If a review comes back FAIL: route the fix back to the *original* Coder
subagent, then re-run only the failed items with the *same two* reviewers.
Do not patch anything yourself.

---

## Step 0 — Re-verify the contract against the actual original file

This bug happened because the Tier A contract was drafted from a "plausible
guess" at what `ble_service.dart` needed, not from the file itself. Before
touching any code:

- Spawn an agent to `grep`/read the **pre-migration** `ble_service.dart`
  (check git history if the current version has already been partially
  rewritten) and list every public method, every `QualifiedCharacteristic`
  subscription, and every write operation it performs.
- Cross-check that list against the command/event list already drafted in
  `docs/isolate_contract.md` (below). Flag anything present in the original
  file but missing from the contract — don't assume the drafted list below is
  exhaustive just because it's detailed.
- Only once this is confirmed complete does Tier B coding start.

---

## Step 1 — Contract expansion (must land before any code changes)

Update `docs/isolate_contract.md` with:

- **New UI→background commands**: `start_scan`, `stop_scan`, `write_bearing`,
  `write_distance`, `write_radar_mode`, `write_config`, `send_calib_cmd`,
  `sync_time`, `write_wake_threshold`, `pause_for_ota`, `resume_after_ota`.
- **New background→UI events**: `ble_scan_result`, `haptic_tx_received`,
  `battery_update`, `radar_mode_update`, `calib_status_update`,
  `calib_threshold_update`, `imu_stream_update`, `compass_stream_update`,
  `ota_ready`.
- **Explicit rules, stated as rules (not left implicit in prose elsewhere)**:
  - IMU/compass: background subscribes at full native rate; only the
    `SendPort` forwarding to UI is throttled to a max of 2/sec (500ms). Internal
    calibration logic always sees full-rate data.
  - The new events above (except `ota_ready`, which is a handshake signal,
    not telemetry) are ephemeral UI-only state — never queued in the Supabase
    ring buffer.
  - `fullState` payload is expanded to include current snapshots of battery,
    radar mode, calib status/threshold, and a `paused_for_ota` boolean.
  - OTA handoff sequence, stated as an ordered protocol:
    1. UI dispatches `pause_for_ota`.
    2. Background drops GATT, disables auto-reconnect, dispatches `ota_ready`.
    3. UI waits on `ota_ready` specifically — never a fixed delay/timer —
       before opening its own local connection.
    4. UI performs the flash inside a `try/finally`.
    5. `resume_after_ota` is dispatched in the `finally` block regardless of
       success, failure, or cancellation.
    6. On any fresh `request_full_state` response, if `paused_for_ota` is true
       but the current UI session has no active OTA flow, the UI immediately
       dispatches `resume_after_ota` to self-heal a stale suspension (e.g. from
       a prior kill mid-flash).

Have the human (you) read this before Tier B starts — same as the original
Contract Doc step.

---

## Step 2 — Tier B: `background_engine.dart`

**Coder task**: implement all new command handling, characteristic
subscriptions, the `pause_for_ota`/`ota_ready` sequence, the SendPort-only
throttle, and the expanded `_sendFullState()`.

**Task Reviewer checklist (2 independent agents):**
- [ ] `pause_for_ota` handler: does it actually drop the GATT connection,
      disable reconnection attempts, *and* dispatch `ota_ready` — all three,
      in that order, not just some of them?
- [ ] Is the IMU/compass throttle applied only at the point of sending over
      `SendPort`? Confirm the internal subscription/processing path still
      receives full-rate data (i.e. throttling isn't accidentally applied to
      the native subscription itself).
- [ ] **Reconnect check**: if BLE disconnects and the engine's native
      reconnect logic fires, does it re-subscribe to *every* characteristic
      (Battery, Radar Mode, Haptic TX, Calib Status, Calib Threshold, IMU,
      Compass) — or only the subset the original single-purpose reconnect
      logic was written for before this expansion? Trace the actual
      reconnect function, don't assume from its name.
- [ ] Does `_sendFullState()` include battery, radar, calib state, and
      `paused_for_ota` in its payload?

**File Reviewer checklist (2 independent agents, whole-file pass):**
- [ ] Any dead subscription/handler left over from a partial implementation?
- [ ] Any state that can end up contradictory — e.g. `paused_for_ota == true`
      while a reconnect timer is still running?
- [ ] Consistent error handling across all new characteristic write/subscribe
      calls (not just the ones added first)?

---

## Step 3 — Tier C: `ble_service.dart`

**Coder task**: strip `flutter_reactive_ble` from every path except
`triggerOTA`; convert everything else to command dispatch; implement the
ack-based OTA wait, the `try/finally` resume guarantee, and the stale-pause
auto-recovery.

**Task Reviewer checklist (2 independent agents):**
- [ ] Is `flutter_reactive_ble` (or `QualifiedCharacteristic`, or any native
      scan/connect call) present *anywhere* outside the scoped `triggerOTA`
      method? Grep the whole file, not just the method that was the focus of
      the last rewrite.
- [ ] Does `triggerOTA` block on receiving the actual `ota_ready` message —
      check for any `Future.delayed`/timer-based wait standing in for it,
      which would silently reintroduce the dual-connection race.
- [ ] Is the flash operation wrapped in `try/finally` with `resume_after_ota`
      in the `finally` block specifically (not just called at the end of the
      success path, and not just in a `catch`)?
- [ ] Does `processBackgroundMessage` check `paused_for_ota` against whether
      *this fresh UI session* has an active OTA flow, and auto-dispatch
      `resume_after_ota` when it's stale? Confirm this doesn't accidentally
      fire during a legitimate, currently-in-progress OTA from the same
      session.

**File Reviewer checklist (2 independent agents, whole-file pass):**
- [ ] Any duplicate state variables left over from the old direct-BLE
      implementation that are no longer written to but still read somewhere?
- [ ] Any dead timers/subscriptions from the pre-refactor version?
- [ ] Does every public method now consistently go through the command
      dispatcher (aside from the one deliberate OTA exception)?

---

## Step 4 — Global Reviewer (after both Tier B and Tier C individually pass)

- [ ] Grep both files: every command name and event name used in
      `ble_service.dart` matches, string-for-string, what
      `background_engine.dart` actually emits/handles.
- [ ] Confirm `flutter_reactive_ble` (or equivalent) is not imported in more
      than the two expected places (the background engine, and the scoped OTA
      method in `ble_service.dart`) across the whole `lib/services` directory.
- [ ] Confirm the OTA ack sequence is symmetric: every command the UI sends
      (`pause_for_ota`) has a corresponding handler in the background engine,
      and every event the UI waits on (`ota_ready`) is actually dispatched
      from that handler.
- [ ] Run `flutter analyze` and paste the actual output as evidence — not a
      description of having run it.

**This step is not optional for any file, regardless of how small the change
looked going in.**

---

## Step 5 — Manual verification (device required, not agent-verifiable)

1. Connect, then trigger scan/bearing/distance/radar/config/calib commands —
   confirm each round-trips correctly through the background isolate.
2. Force a mid-stream BLE disconnect/reconnect (e.g. walk out of range and
   back) — confirm *all* characteristics resume updating, not just some.
3. Start an OTA flash, then force-kill the app mid-transfer. Reopen the app
   and confirm the background engine auto-resumes (BLE/GPS telemetry
   resumes) without requiring a device reboot.
4. Trigger an OTA and induce a failure partway (e.g. disconnect the watch
   mid-flash) — confirm `resume_after_ota` still fires and normal background
   operation resumes afterward.
5. Confirm IMU/compass-driven UI elements update at a reasonable, non-janky
   rate without excessive battery drain over a longer session.

Attach logcat excerpts or screen recordings for 2–4 as Antigravity artifacts —
don't accept a text claim that these passed.
