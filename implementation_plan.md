# Background Execution Migration Plan

The goal is to fix the issue where the app's BLE connection drops and GPS stops polling when the UI isolate is suspended or killed (e.g. when the user swipes the app away from recent apps). 

To solve this, we will move the entire execution engine into the background isolate managed by `flutter_foreground_task`.

## User Review Required
> [!IMPORTANT]
> **Supabase Limitation**: The background isolate cannot reliably use `supabase_flutter` because it relies on Flutter's UI engine (`WidgetsBinding`). We will instantiate a pure Dart `SupabaseClient` (from the `supabase` package) in the background and pass the auth token from the UI isolate.

> [!TIP]
> **BLE and GPS Reliability**: Our agent team has verified that `flutter_reactive_ble` and `geolocator` will function seamlessly in the Android background isolate. To ensure they survive when you swipe the app away, we will add `android:stopWithTask="false"` to the service manifest.

## Scope & Constraints
> [!NOTE]
> **Out of Scope**: 
> - **iOS**: This migration specifically uses Android's Foreground Services. iOS background BLE/Location uses entirely different native mechanisms and is out of scope for this plan.
> - **Force Stop**: If a user explicitly "Force Stops" the app from Android Settings, Android intentionally blocks all restart mechanisms. The background task will die and not resurrect.

## Proposed Changes

We will introduce a `BackgroundEngine` that runs entirely inside the `TaskHandler`'s isolate. The Main Isolate (UI) will act as a thin client that listens to the `BackgroundEngine` and renders the data.

### 0. `app/android/app/src/main/AndroidManifest.xml`
- **[VERIFIED]**: The manifest already correctly declares `FOREGROUND_SERVICE_LOCATION`, `FOREGROUND_SERVICE_CONNECTED_DEVICE`, sets the `foregroundServiceType`, AND explicitly declares the `RECEIVE_BOOT_COMPLETED` permission alongside the `BootReceiver` entry.
- **[MODIFY]**: Add `android:stopWithTask="false"` to the ForegroundService declaration to prevent Android from killing the background isolate when the app is swiped away from Recents.

### 1. `app/lib/services/foreground_service.dart`
- **[MODIFY]**: Redesign `_ForegroundTaskHandler`. Its `onStart` will initialize a new `BackgroundEngine`, starting with `WidgetsFlutterBinding.ensureInitialized()` and `DartPluginRegistrant.ensureInitialized()` to prevent platform channel crashes.
- **[MODIFY]**: Set up robust v6+ bi-directional communication. The UI isolate will safely send commands (`connect_ble`, `update_role`, `request_full_state`, `stop_engine`) and receive state even after being restarted.
- **[MODIFY]**: Implement the "App Restart Handshake". When the UI boots, it immediately sends `request_full_state`. The background isolate uses its command queue to buffer this on cold-starts, or answers instantly on warm-reattaches, preventing any UI hanging.

### 2. `app/lib/services/background_engine.dart`
- **[NEW]**: Create this new file to host the background execution logic.
- **[NEW] Global Error Boundary**: Wrap the entire initialization and event loops in `PlatformDispatcher.instance.onError` to prevent fatal isolate crashes from causing an infinite foreground service restart loop.
- **[NEW] Boot Self-Hydration**: On boot, the engine independently reads both the Supabase tokens AND the `ble_mac_address` from `SharedPreferences`. **Gate condition**: If the tokens are absent/invalid (e.g. user was logged out), it completely skips startup and immediately calls `FlutterForegroundTask.stopService()` so the user doesn't get a persistent notification with no active session.
- **[NEW] Throttled Telemetry Buffer**: Since Android Doze denies background network access, `SocketException`s queue data locally in an explicit ring buffer (capped at ~100 items). Upon reconnect, items are **chronologically flushed and throttled** to match the normal polling cadence to avoid burst-flushing that would spike rate limits and battery usage.
- **[NEW] Supabase Token Rotation & Infinite 401 Prevention**: Intercept `401 Unauthorized` errors to manually call `refreshSession()`. If the refresh permanently fails (token revoked), push a "session_expired" state to the UI AND **internally invoke `stop_engine`** to completely shut down BLE and GPS polling, as the session is dead. Upon successful rotation, save to `SharedPreferences` and pipe new tokens to the UI isolate.
- **[NEW] Teardown Lifecycle**: Handle a `stop_engine`/`logout` command to cleanly kill BLE streams, stop GPS, and trigger `FlutterForegroundTask.stopService()`. **Crucially**, sequence this by sending the `session_expired`/teardown state over the port *first*, awaiting a short delay or ack to ensure it flushes, and *then* tearing down the service, preventing the UI from hanging on a dead port.

### 3. `app/lib/services/ble_service.dart` & `location_service.dart`
- **[MODIFY]**: Refactor these to act as thin proxies in the Main Isolate. Instead of executing BLE/GPS calls directly, they will send commands to the Foreground Service and listen to its events to update the UI.

### 4. `app/lib/main.dart` & `app/lib/screens/home_screen.dart`
- **[MODIFY]**: Prevent Android 14 `SecurityException` crashes by ensuring `ForegroundService.start()` is **only called AFTER** the user has explicitly granted both `ACCESS_FINE_LOCATION` and `BLUETOOTH_CONNECT` permissions.
- **[MODIFY]**: Configure `flutter_foreground_task` to acquire a **wakelock** (`isOnceEvent` or task options) and explicitly enable `autoRunOnBoot` so the service cleanly resurrects after a device restart.
- **[MODIFY]**: **Single Auth Master & 401 Escape Hatch**: Ensure any UI-isolate `SupabaseClient` has `autoRefreshToken: false`. Implement a 401 interceptor in the UI that pauses requests and waits for *either* an incoming token port-update from the background (to retry) *or* a `session_expired` signal (to surface the logout state), preventing infinite UI hangs.

## Verification Plan

### Automated Tests
- Run `flutter analyze` to ensure no isolate syntax errors or type mismatches across ports.

### Manual Verification
1. **Aggressive OEM Test**: Run on a Xiaomi/Samsung/EMUI device to verify OEM background managers don't kill the service when swiped away.
2. **Swipe Away**: Swipe the app away from recent apps and verify the foreground notification remains active.
3. **Data Safety**: Verify cross-isolate data payloads only use message-safe primitives.
4. **Reboot Survival**: Reboot the device with the screen off and verify the `BootReceiver` correctly resurrects the background polling engine AND the Foreground Service actually starts successfully, explicitly bypassing the Android 12+ background start restriction silently failing.
5. **Force Stop Confirmation**: Force Stop the app from settings to explicitly verify it stays dead (expected behavior).
