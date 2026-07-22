- `[x]` **1. Prep Work & Permissions**
  - `[x]` Modify `app/android/app/src/main/AndroidManifest.xml` to add `android:stopWithTask="false"` to the foreground service declaration.
  - `[x]` Update `pubspec.yaml` to ensure pure `supabase` is available if needed, or simply update imports to avoid UI dependencies.
  - `[x]` Refactor `AppBootstrapper` in `main.dart` so `ForegroundService.start()` is only called *after* all GPS and BLE permissions are explicitly granted by the user.
  - `[x]` Disable `autoRefreshToken: false` in the UI isolate's `SupabaseClient` to establish the background isolate as the single source of truth for token refreshes.
  - `[x]` Add a `401 Unauthorized` interceptor to the UI isolate's `SupabaseClient` that explicitly pauses the request, waits for an incoming token port-update OR a `session_expired` signal, and retries or logs out accordingly.
  - `[x]` Configure `flutter_foreground_task` options in `main.dart` to explicitly enable `autoRunOnBoot` and acquire a partial wakelock.
  - `[x]` Ensure the UI explicitly saves the `ble_mac_address` to `SharedPreferences` upon the first successful connection to allow background reboot recovery.

- `[x]` **2. Core Engine Migration (`background_engine.dart`)**
  - `[x]` Create `app/lib/services/background_engine.dart` and wrap the entire class inside a global `PlatformDispatcher.instance.onError` boundary to prevent fatal isolate crash loops.
  - `[x]` Implement `WidgetsFlutterBinding.ensureInitialized()` and `DartPluginRegistrant.ensureInitialized()` in the initialization phase.
  - `[x]` Implement Boot Self-Hydration: independently read tokens and `ble_mac_address` from `SharedPreferences` on boot. Include a gate check: if auth tokens are missing (logged out), skip startup and immediately stop the foreground service.
  - `[x]` Migrate the BLE scanning, connecting, and writing logic from `BleService`.
  - `[x]` Migrate the GPS dynamic polling loop logic from `LocationService`.
  - `[x]` Migrate the Supabase real-time sync logic using a pure Dart `SupabaseClient`.
  - `[x]` Implement a local Telemetry Ring Buffer (capped at ~100 items) to queue data on `SocketException`. Implement logic to flush the buffer chronologically and throttled (matching normal polling intervals) upon network restoration to avoid burst rate-limiting.
  - `[x]` Implement a robust `401 Unauthorized` interceptor. Handle infinite 401 loops by clearing tokens, pushing a `session_expired` state to the UI, AND invoking internal `stop_engine` logic to cleanly kill BLE/GPS since the session is unrecoverable.
  - `[x]` Implement `supabase.auth.onAuthStateChange` listener to save rotated tokens to `SharedPreferences` and pipe them to the UI isolate.
  - `[x]` Implement a command queue/buffer inside `BackgroundEngine` to hold incoming UI commands until asynchronous initialization completes.

- `[x]` **3. Communication Protocol (Isolate Messaging)**
  - `[x]` Implement `FlutterForegroundTask.initCommunicationPort()` in `main.dart` before `runApp`.
  - `[x]` Implement Handshake: UI immediately sends `request_full_state` upon connection. The background command queue inherently protects this against cold-start races.
  - `[x]` Implement `onReceiveData` in `_ForegroundTaskHandler` to process commands (`connect_ble`, `request_full_state`, `stop_engine`).
  - `[x]` Implement teardown lifecycle logic when `stop_engine` is received. Ensure `session_expired` is pushed to the UI and flushed *before* calling `FlutterForegroundTask.stopService()`, preventing the UI interceptor from hanging on a dead port.

- `[x]` **4. UI Thin Proxies Refactor**
  - `[x]` Refactor `BleService` (`ble_service.dart`) to act as a UI proxy. It should send commands (`connect`, `write_haptic`, `request_full_state`) to the foreground task via `FlutterForegroundTask.sendDataToTask()`.
  - `[x]` Refactor `LocationService` (`location_service.dart`) to act as a UI proxy, receiving GPS coordinates from the isolate rather than querying the OS itself.
  - `[x]` Refactor `SyncService` (`sync_service.dart`) if necessary, ensuring the UI still displays the sync status properly.

- `[ ]` **5. Testing & Validation**
  - `[ ]` Verify cross-isolate payloads contain only message-safe primitive types.
  - `[ ]` Test on an aggressive OEM device (e.g., Xiaomi/Samsung) to verify swipe-away survival.
  - `[ ]` Test device Reboot to verify `BootReceiver` resurrects the engine AND confirm the FGS actively starts with the screen off (bypassing Android 12+ background restrictions).
  - `[ ]` Test Android Settings "Force Stop" to explicitly verify the service dies completely (Expected behavior).
  - `[x]` Run `flutter analyze` to fix any syntax errors or bad imports (e.g. `dart:ui` inside the background isolate).
  - `[ ]` Verify Android 14 FGS starts without crashing.
  - `[ ]` Verify BLE connection persists after swiping the app away.
