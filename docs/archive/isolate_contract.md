# Isolate Communication Contract

This document explicitly defines the contract, payloads, keys, and operational rules governing the boundary between the UI isolate and the Background Engine (`flutter_foreground_task`).

## 1. Commands (UI -> Background)
The UI isolate sends these commands via `FlutterForegroundTask.sendDataToTask()`. The payloads must be strict JSON/Map representations.

- **`request_full_state`**
  - **Purpose**: Sent on boot or reconnect to get the current engine state.
  - **Payload Shape**:
    ```json
    {
      "type": "request_full_state",
      "payload": {}
    }
    ```

- **`connect_ble`**
  - **Purpose**: Initiates BLE connection to a specific device.
  - **Payload Shape**:
    ```json
    {
      "type": "connect_ble",
      "payload": {
        "mac_address": "AA:BB:CC:DD:EE:FF"
      }
    }
    ```

- **`update_role`**
  - **Purpose**: Updates the user's role/status in the background process.
  - **Payload Shape**:
    ```json
    {
      "type": "update_role",
      "payload": {
        "role": "string"
      }
    }
    ```

- **`write_haptic`**
  - **Purpose**: Triggers a haptic pattern on the connected BLE device.
  - **Payload Shape**:
    ```json
    {
      "type": "write_haptic",
      "payload": {
        "pattern": "string"
      }
    }
    ```

- **`stop_engine`**
  - **Purpose**: Cleanly shuts down the background engine.
  - **Payload Shape**:
    ```json
    {
      "type": "stop_engine",
      "payload": {}
    }
    ```

- **`start_scan`**
  - **Purpose**: Initiates BLE device scanning.
  - **Payload Shape**:
    ```json
    {
      "type": "start_scan",
      "payload": {}
    }
    ```

- **`stop_scan`**
  - **Purpose**: Stops BLE device scanning.
  - **Payload Shape**:
    ```json
    {
      "type": "stop_scan",
      "payload": {}
    }
    ```

- **`write_bearing`**
  - **Purpose**: Writes the bearing value to the watch.
  - **Payload Shape**:
    ```json
    {
      "type": "write_bearing",
      "payload": {
        "bearing": 0.0
      }
    }
    ```

- **`write_distance`**
  - **Purpose**: Writes the distance in meters to the watch.
  - **Payload Shape**:
    ```json
    {
      "type": "write_distance",
      "payload": {
        "distance": 0
      }
    }
    ```

- **`write_radar_mode`**
  - **Purpose**: Enables or disables radar mode on the watch.
  - **Payload Shape**:
    ```json
    {
      "type": "write_radar_mode",
      "payload": {
        "active": false
      }
    }
    ```

- **`write_config`**
  - **Purpose**: Writes the serialized config JSON to the watch.
  - **Payload Shape**:
    ```json
    {
      "type": "write_config",
      "payload": {
        "config_json": "{...}"
      }
    }
    ```

- **`send_calib_cmd`**
  - **Purpose**: Sends a calibration or streaming command byte to the watch.
  - **Payload Shape**:
    ```json
    {
      "type": "send_calib_cmd",
      "payload": {
        "cmd": 0
      }
    }
    ```

- **`sync_time`**
  - **Purpose**: Syncs current Unix timestamp and timezone offset to the watch.
  - **Payload Shape**:
    ```json
    {
      "type": "sync_time",
      "payload": {}
    }
    ```

- **`write_wake_threshold`**
  - **Purpose**: Writes the wake-on-motion threshold.
  - **Payload Shape**:
    ```json
    {
      "type": "write_wake_threshold",
      "payload": {
        "threshold": 0
      }
    }
    ```

- **`pause_for_ota`**
  - **Purpose**: Instructs the background engine to drop GATT and disable reconnects so the UI can flash OTA.
  - **Payload Shape**:
    ```json
    {
      "type": "pause_for_ota",
      "payload": {}
    }
    ```

- **`resume_after_ota`**
  - **Purpose**: Instructs the background engine to resume its GATT connection and normal operation after an OTA completes or fails.
  - **Payload Shape**:
    ```json
    {
      "type": "resume_after_ota",
      "payload": {}
    }
    ```

## 2. Messages (Background -> UI)
The Background Engine sends these events to the UI via `SendPort`. The UI intercepts them via `onReceiveData`.

- **`fullState`**
  - **Purpose**: Reply to `request_full_state`, providing snapshot of everything.
  - **Payload Shape**:
    ```json
    {
      "type": "fullState",
      "payload": {
        "bleState": "string",
        "macAddress": "string",
        "gpsLat": 0.0,
        "gpsLng": 0.0,
        "syncStatus": "string",
        "batteryPercent": 100,
        "radarModeActive": false,
        "calibStatus": {"example": 0},
        "calibThreshold": 0,
        "paused_for_ota": false
      }
    }
    ```

- **`token_update`**
  - **Purpose**: Pushed when the background engine rotates Supabase tokens.
  - **Payload Shape**:
    ```json
    {
      "type": "token_update",
      "payload": {
        "access_token": "string",
        "refresh_token": "string"
      }
    }
    ```

- **`session_expired`**
  - **Purpose**: Pushed when token rotation fails permanently (401 unrecoverable).
  - **Payload Shape**:
    ```json
    {
      "type": "session_expired",
      "payload": {}
    }
    ```

- **`syncError`**
  - **Purpose**: Pushed when telemetry sync fails permanently.
  - **Payload Shape**:
    ```json
    {
      "type": "syncError",
      "payload": {
        "message": "string",
        "code": "string"
      }
    }
    ```

- **`ble_update`**
  - **Purpose**: Live BLE connection state updates.
  - **Payload Shape**:
    ```json
    {
      "type": "ble_update",
      "payload": {
        "status": "string",
        "device": "string"
      }
    }
    ```

- **`gps_update`**
  - **Purpose**: Live GPS coordinate updates.
  - **Payload Shape**:
    ```json
    {
      "type": "gps_update",
      "payload": {
        "lat": 0.0,
        "lng": 0.0
      }
    }
    ```

- **`ble_scan_result`**
  - **Purpose**: Returns a discovered BLE device during a scan.
  - **Payload Shape**:
    ```json
    {
      "type": "ble_scan_result",
      "payload": {
        "id": "string",
        "name": "string",
        "rssi": 0
      }
    }
    ```

- **`haptic_tx_received`**
  - **Purpose**: Emitted when the watch notifies that the user tapped it.
  - **Payload Shape**:
    ```json
    {
      "type": "haptic_tx_received",
      "payload": {}
    }
    ```

- **`battery_update`**
  - **Purpose**: Emitted when the watch sends a new battery percentage.
  - **Payload Shape**:
    ```json
    {
      "type": "battery_update",
      "payload": {
        "percent": 100
      }
    }
    ```

- **`radar_mode_update`**
  - **Purpose**: Emitted when the watch toggles radar mode.
  - **Payload Shape**:
    ```json
    {
      "type": "radar_mode_update",
      "payload": {
        "active": false
      }
    }
    ```

- **`calib_status_update`**
  - **Purpose**: Emitted when the watch sends calibration progress/status.
  - **Payload Shape**:
    ```json
    {
      "type": "calib_status_update",
      "payload": {
        "status": {"example": 0}
      }
    }
    ```

- **`calib_threshold_update`**
  - **Purpose**: Emitted when the watch sends its current wake-on-motion threshold.
  - **Payload Shape**:
    ```json
    {
      "type": "calib_threshold_update",
      "payload": {
        "threshold": 0
      }
    }
    ```

- **`imu_stream_update`**
  - **Purpose**: Live IMU data from the watch (throttled at SendPort boundary).
  - **Payload Shape**:
    ```json
    {
      "type": "imu_stream_update",
      "payload": {
        "data": {"x": 0, "y": 0, "z": 0}
      }
    }
    ```

- **`compass_stream_update`**
  - **Purpose**: Live Compass data from the watch (throttled at SendPort boundary).
  - **Payload Shape**:
    ```json
    {
      "type": "compass_stream_update",
      "payload": {
        "heading": 0.0
      }
    }
    ```

- **`ota_ready`**
  - **Purpose**: Acknowledgment from the background engine that it has dropped GATT and is safe for the UI to perform an OTA flash.
  - **Payload Shape**:
    ```json
    {
      "type": "ota_ready",
      "payload": {}
    }
    ```

## 3. SharedPreferences Keys
Both isolates rely on identical string literal keys to hydrate state.
- **BLE MAC Address**: `"ble_mac_address"`
- **Access Token**: `"supabase_access_token"`
- **Refresh Token**: `"supabase_refresh_token"`

## 4. Operational Rules

### 4.1 Teardown Sequencing Rule
When terminating the service, the `session_expired` (or equivalent final state message) **must be sent and fully acknowledged/flushed over the port before `stopService()` is called**. Calling `stopService()` too early will instantly tear down the isolate and sever the port, causing the UI interceptor to hang indefinitely.

### 4.2 Boot Gate Rule
On boot, the background engine must **first** verify the presence of a valid auth token in `SharedPreferences`. If no valid token is found, the engine **must immediately call `FlutterForegroundTask.stopService()`** and abort all initialization.
Additionally, the background isolate must independently call `await dotenv.load(fileName: '.env')` to retrieve `SUPABASE_URL` and `SUPABASE_ANON_KEY` to instantiate the pure Dart `SupabaseClient`.

### 4.3 Ring Buffer Rule
When offline/encountering `SocketException`, telemetry data is pushed to an explicit Ring Buffer with the following constraints:
- **Cap**: Maximum of 100 items.
- **Eviction**: Drops the oldest item when full.
- **Flush Order**: Strict chronological order (oldest to newest).
- **Flush Pacing**: Throttled to match the normal polling interval. Burst flushing is strictly forbidden to avoid rate limits and massive battery drain.

### 4.4 401 UI-Side Wait Rule
When the UI-side `SupabaseClient` hits a `401 Unauthorized`, the interceptor must pause the request. The Future completing the interceptor **must wait for either of two events to resolve it**:
1. An incoming `token_update` message.
2. An incoming `session_expired` message.
**Both** events must be capable of resolving the wait state. If only one resolves it and the other fires, the UI will hang indefinitely.

### 4.5 IMU/Compass Decimation Rule
The background engine subscribes to IMU and compass characteristics at their full native rate. However, to preserve CPU and battery, the events forwarded to the UI over the `SendPort` (`imu_stream_update` and `compass_stream_update`) **must be explicitly throttled to a maximum of 2 per second (500ms)**. Internal background calibration logic always sees full-rate data.

### 4.6 Ephemeral UI-Only State Rule
The new BLE events (`battery_update`, `radar_mode_update`, `calib_status_update`, `calib_threshold_update`, `imu_stream_update`, `compass_stream_update`, `haptic_tx_received`) are explicitly ephemeral. They must **never** be queued in the Supabase telemetry Ring Buffer. If the UI `SendPort` is not listening or offline, these events are safely discarded.

### 4.7 OTA Handoff Sequence Protocol
OTA flashing occurs exclusively in the UI isolate to minimize the risk of a mid-transfer kill. The handoff follows this strict ordered protocol:
1. UI dispatches `pause_for_ota`.
2. Background drops GATT, disables auto-reconnect, and dispatches `ota_ready`.
3. UI waits on `ota_ready` specifically — never a fixed delay/timer — before opening its own local connection.
4. UI performs the flash inside a strict `try/finally` block.
5. `resume_after_ota` is dispatched in the `finally` block regardless of success, failure, or cancellation.
6. On any fresh `request_full_state` response, if `paused_for_ota` is true but the current UI session has no active OTA flow, the UI immediately dispatches `resume_after_ota` to self-heal a stale suspension (e.g. from a prior kill mid-flash).
