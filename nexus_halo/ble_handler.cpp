#include "ble_handler.h"

// UUIDs (16-bit uint16_t)
// Service UUID is custom 128-bit: 4A5C180A-5F2D-4E1B-822C-4A2D87B4C85B
uint8_t const BLE_SERVICE_UUID_128[16] = {
  0x5B, 0xC8, 0xB4, 0x87, 0x2D, 0x4A, 0x2C, 0x82,
  0x1B, 0x4E, 0x2D, 0x5F, 0x0A, 0x18, 0x5C, 0x4A
};
uint8_t const BLE_BEARING_UUID_128[16] = {
  0x5B, 0xC8, 0xB4, 0x87, 0x2D, 0x4A, 0x2C, 0x82, 0x1B, 0x4E, 0x2D, 0x5F, 0x58, 0x2A, 0x5C, 0x4A
};
uint8_t const BLE_DISTANCE_UUID_128[16] = {
  0x5B, 0xC8, 0xB4, 0x87, 0x2D, 0x4A, 0x2C, 0x82, 0x1B, 0x4E, 0x2D, 0x5F, 0x59, 0x2A, 0x5C, 0x4A
};
uint8_t const BLE_HAPTIC_TX_UUID_128[16]       = { 0x5B, 0xC8, 0xB4, 0x87, 0x2D, 0x4A, 0x2C, 0x82, 0x1B, 0x4E, 0x2D, 0x5F, 0x5A, 0x2A, 0x5C, 0x4A };
uint8_t const BLE_HAPTIC_RX_UUID_128[16]       = { 0x5B, 0xC8, 0xB4, 0x87, 0x2D, 0x4A, 0x2C, 0x82, 0x1B, 0x4E, 0x2D, 0x5F, 0x5B, 0x2A, 0x5C, 0x4A };
#define BLE_BATTERY_UUID          0x2A19
uint8_t const BLE_RADAR_MODE_UUID_128[16]      = { 0x5B, 0xC8, 0xB4, 0x87, 0x2D, 0x4A, 0x2C, 0x82, 0x1B, 0x4E, 0x2D, 0x5F, 0x5F, 0x2A, 0x5C, 0x4A };
uint8_t const BLE_CONFIG_UUID_128[16]          = { 0x5B, 0xC8, 0xB4, 0x87, 0x2D, 0x4A, 0x2C, 0x82, 0x1B, 0x4E, 0x2D, 0x5F, 0x60, 0x2A, 0x5C, 0x4A };
uint8_t const BLE_CALIB_CMD_UUID_128[16]       = { 0x5B, 0xC8, 0xB4, 0x87, 0x2D, 0x4A, 0x2C, 0x82, 0x1B, 0x4E, 0x2D, 0x5F, 0x5C, 0x2A, 0x5C, 0x4A };
uint8_t const BLE_CALIB_STATUS_UUID_128[16]    = { 0x5B, 0xC8, 0xB4, 0x87, 0x2D, 0x4A, 0x2C, 0x82, 0x1B, 0x4E, 0x2D, 0x5F, 0x5D, 0x2A, 0x5C, 0x4A };
uint8_t const BLE_CALIB_THRESHOLD_UUID_128[16] = { 0x5B, 0xC8, 0xB4, 0x87, 0x2D, 0x4A, 0x2C, 0x82, 0x1B, 0x4E, 0x2D, 0x5F, 0x5E, 0x2A, 0x5C, 0x4A };
uint8_t const BLE_OTA_UUID_128[16]             = { 0x5B, 0xC8, 0xB4, 0x87, 0x2D, 0x4A, 0x2C, 0x82, 0x1B, 0x4E, 0x2D, 0x5F, 0x61, 0x2A, 0x5C, 0x4A };
uint8_t const BLE_TIME_SYNC_UUID_128[16] = {
  0x5B, 0xC8, 0xB4, 0x87, 0x2D, 0x4A, 0x2C, 0x82, 0x1B, 0x4E, 0x2D, 0x5F, 0x2B, 0x2A, 0x5C, 0x4A
};
uint8_t const BLE_IMU_STREAM_UUID_128[16] = {
  0x5B, 0xC8, 0xB4, 0x87, 0x2D, 0x4A, 0x2C, 0x82, 0x1B, 0x4E, 0x2D, 0x5F, 0x62, 0x2A, 0x5C, 0x4A
};
uint8_t const BLE_COMPASS_STREAM_UUID_128[16] = {
  0x5B, 0xC8, 0xB4, 0x87, 0x2D, 0x4A, 0x2C, 0x82, 0x1B, 0x4E, 0x2D, 0x5F, 0x64, 0x2A, 0x5C, 0x4A
};

BLEHandler* BLEHandler::instance = nullptr;

BLEHandler::BLEHandler()
  : ble_connected(false),
    ble_init_ok(false),
    service(BLE_SERVICE_UUID_128),
    bearing_char(BLE_BEARING_UUID_128),
    distance_char(BLE_DISTANCE_UUID_128),
    haptic_tx_char(BLE_HAPTIC_TX_UUID_128),
    haptic_rx_char(BLE_HAPTIC_RX_UUID_128),
    radar_mode_char(BLE_RADAR_MODE_UUID_128),
    config_char(BLE_CONFIG_UUID_128),
    battery_char(BLE_BATTERY_UUID),
    calib_cmd_char(BLE_CALIB_CMD_UUID_128),
    calib_status_char(BLE_CALIB_STATUS_UUID_128),
    calib_threshold_char(BLE_CALIB_THRESHOLD_UUID_128),
    ota_char(BLE_OTA_UUID_128),
    time_sync_char(BLE_TIME_SYNC_UUID_128),
    imu_stream_char(BLE_IMU_STREAM_UUID_128),
    compass_stream_char(BLE_COMPASS_STREAM_UUID_128),
    last_bearing(0),
    last_distance(0),
    radar_mode_requested(false),
    calib_threshold(IMU_WAKE_UP_THS_DEFAULT),
    callback_haptic_rx(nullptr),
    callback_bearing_update(nullptr),
    callback_distance_update(nullptr),
    callback_config_update(nullptr),
    callback_calib_start(nullptr),
    callback_calib_end(nullptr),
    callback_calib_cancel(nullptr),
    callback_compass_calib_start(nullptr),
    callback_threshold_write(nullptr),
    callback_ota_request(nullptr),
    callback_time_sync(nullptr),
    conn_param_requested(false),
    imu_stream_requested(false),
    compass_stream_requested(false)
{
  instance = this;
  memset(config_json_buf, 0, sizeof(config_json_buf));
}

void BLEHandler::begin() {
  json_mutex = xSemaphoreCreateMutex();
  Bluefruit.configPrphBandwidth(BANDWIDTH_MAX); // Request up to 247 MTU
  if (!Bluefruit.begin()) {
    Serial.println("[BLE] ✗ Bluefruit.begin() failed");
    ble_init_ok = false;
    return;
  }
  ble_init_ok = true;
  
  Bluefruit.setName(BLE_DEVICE_NAME);
  Bluefruit.Periph.setConnectCallback(connect_callback);
  Bluefruit.Periph.setDisconnectCallback(disconnect_callback);

  service.begin();

  // Bearing: read, write, notify
  bearing_char.setProperties(CHR_PROPS_READ | CHR_PROPS_WRITE | CHR_PROPS_NOTIFY);
  bearing_char.setPermission(SECMODE_OPEN, SECMODE_OPEN);
  bearing_char.setFixedLen(4);
  bearing_char.setWriteCallback(write_callback);
  bearing_char.begin();

  // Distance: read, write, notify
  distance_char.setProperties(CHR_PROPS_READ | CHR_PROPS_WRITE | CHR_PROPS_NOTIFY);
  distance_char.setPermission(SECMODE_OPEN, SECMODE_OPEN);
  distance_char.setFixedLen(4);
  distance_char.setWriteCallback(write_callback);
  distance_char.begin();

  // Haptic TX: read, notify
  haptic_tx_char.setProperties(CHR_PROPS_READ | CHR_PROPS_NOTIFY);
  haptic_tx_char.setPermission(SECMODE_OPEN, SECMODE_NO_ACCESS);
  haptic_tx_char.setFixedLen(1);
  haptic_tx_char.begin();

  // Haptic RX: read, write
  haptic_rx_char.setProperties(CHR_PROPS_READ | CHR_PROPS_WRITE);
  haptic_rx_char.setPermission(SECMODE_OPEN, SECMODE_OPEN);
  haptic_rx_char.setFixedLen(1);
  haptic_rx_char.setWriteCallback(write_callback);
  haptic_rx_char.begin();

  // Radar mode: read, write, notify
  radar_mode_char.setProperties(CHR_PROPS_READ | CHR_PROPS_WRITE | CHR_PROPS_NOTIFY);
  radar_mode_char.setPermission(SECMODE_OPEN, SECMODE_OPEN);
  radar_mode_char.setFixedLen(1);
  radar_mode_char.setWriteCallback(write_callback);
  radar_mode_char.begin();

  // Config: read, write (max 256)
  config_char.setProperties(CHR_PROPS_READ | CHR_PROPS_WRITE | CHR_PROPS_NOTIFY);
  config_char.setPermission(SECMODE_OPEN, SECMODE_OPEN);
  config_char.setMaxLen(sizeof(config_json_buf) - 1);
  config_char.setWriteCallback(write_callback);
  config_char.begin();

  // Battery: read, notify
  battery_char.setProperties(CHR_PROPS_READ | CHR_PROPS_NOTIFY);
  battery_char.setPermission(SECMODE_OPEN, SECMODE_NO_ACCESS);
  battery_char.setFixedLen(1);
  battery_char.begin();

  // Calib CMD: read, write
  calib_cmd_char.setProperties(CHR_PROPS_READ | CHR_PROPS_WRITE);
  calib_cmd_char.setPermission(SECMODE_OPEN, SECMODE_OPEN);
  calib_cmd_char.setFixedLen(1);
  calib_cmd_char.setWriteCallback(write_callback);
  calib_cmd_char.begin();

  // Calib Status: read, notify
  calib_status_char.setProperties(CHR_PROPS_READ | CHR_PROPS_NOTIFY);
  calib_status_char.setPermission(SECMODE_OPEN, SECMODE_NO_ACCESS);
  calib_status_char.setFixedLen(2);
  calib_status_char.begin();

  // Calib Threshold: read, write, notify
  calib_threshold_char.setProperties(CHR_PROPS_READ | CHR_PROPS_WRITE | CHR_PROPS_NOTIFY);

  calib_threshold_char.setPermission(SECMODE_OPEN, SECMODE_OPEN);
  calib_threshold_char.setFixedLen(1);
  calib_threshold_char.setWriteCallback(write_callback);
  calib_threshold_char.begin();

  // OTA: read, write
  ota_char.setProperties(CHR_PROPS_READ | CHR_PROPS_WRITE);
  ota_char.setPermission(SECMODE_OPEN, SECMODE_OPEN);
  ota_char.setFixedLen(1);
  ota_char.setWriteCallback(write_callback);
  ota_char.begin();

  // Time Sync: write-only (app sends uint32 Unix timestamp + int32 tz offset, little-endian)
  time_sync_char.setProperties(CHR_PROPS_WRITE);
  time_sync_char.setPermission(SECMODE_NO_ACCESS, SECMODE_OPEN);
  time_sync_char.setFixedLen(8);
  time_sync_char.setWriteCallback(write_callback);
  time_sync_char.begin();

  // IMU Stream: read, notify (mg and dps, 4 bytes total)
  imu_stream_char.setProperties(CHR_PROPS_READ | CHR_PROPS_NOTIFY);
  imu_stream_char.setPermission(SECMODE_OPEN, SECMODE_NO_ACCESS);
  imu_stream_char.setFixedLen(4);
  imu_stream_char.begin();

  compass_stream_char.setProperties(CHR_PROPS_READ | CHR_PROPS_NOTIFY);
  compass_stream_char.setPermission(SECMODE_OPEN, SECMODE_NO_ACCESS);
  compass_stream_char.setFixedLen(4);
  compass_stream_char.begin();

  // Initialize values
  float zero_f = 0.0f;
  bearing_char.write(&zero_f, 4);
  uint32_t zero_u32 = 0;
  distance_char.write(&zero_u32, 4);
  uint8_t batt = 100;
  battery_char.write(&batt, 1);
  calib_threshold_char.write(&calib_threshold, 1);

  // Setup advertising
  Bluefruit.Advertising.addFlags(BLE_GAP_ADV_FLAGS_LE_ONLY_GENERAL_DISC_MODE);
  Bluefruit.Advertising.addTxPower();
  Bluefruit.Advertising.addService(service);
  Bluefruit.ScanResponse.addName();
  
  Bluefruit.Advertising.restartOnDisconnect(true);
  // Interval conversion to 0.625ms units: ms * 8 / 5
  uint32_t adv_interval_units = (BLE_ADVERTISING_INTERVAL_MS * 8) / 5;
  Bluefruit.Advertising.setInterval(adv_interval_units, adv_interval_units);
  Bluefruit.Advertising.setFastTimeout(30);
  Bluefruit.Advertising.start(0);
}

void BLEHandler::update() {
  if (ble_connected && !conn_param_requested && (millis() - conn_timestamp >= 500)) {
    conn_param_requested = true;
    BLEConnection* conn = Bluefruit.Connection(active_conn_handle);
    if (conn) {
      // 84 * 1.25ms = 105ms (multiple of 15ms). Args: interval, latency, timeout
      conn->requestConnectionParameter(84, 0, 400); 
    }
  }
}

void BLEHandler::setLowPowerAdvertising(bool enabled) {
  if (!ble_init_ok) return;
  Serial.print("[BLE] Adjusting advertising interval. Low-power mode: ");
  Serial.println(enabled ? "ENABLED" : "DISABLED");

  uint32_t interval_ms = enabled ? BLE_ADVERTISING_INTERVAL_MS_SLEEP : BLE_ADVERTISING_INTERVAL_MS;
  uint32_t interval_units = (interval_ms * 8) / 5; // Convert ms to 0.625ms units
  Bluefruit.Advertising.setInterval(interval_units, interval_units);

  if (ble_connected) return;

  // We must stop advertising before changing the interval, and then restart
  Bluefruit.Advertising.stop();
  Bluefruit.Advertising.start(0);
}

// Force FreeRTOS to wake the main loop from sleep when BLE events occur
extern void wakeMainLoop();

void BLEHandler::connect_callback(uint16_t conn_handle) {
  if (instance) instance->_onConnect(conn_handle);
  wakeMainLoop();
}

void BLEHandler::disconnect_callback(uint16_t conn_handle, uint8_t reason) {
  if (instance) instance->_onDisconnect(conn_handle, reason);
  wakeMainLoop();
}

void BLEHandler::write_callback(uint16_t conn_handle, BLECharacteristic* chr, uint8_t* data, uint16_t len) {
  if (instance) instance->_onWrite(conn_handle, chr, data, len);
  wakeMainLoop();
}

void BLEHandler::_onConnect(uint16_t conn_handle) {
  ble_connected = true;
  active_conn_handle = conn_handle;
  conn_timestamp = millis();
  conn_param_requested = false;
  radar_mode_requested = false;
  imu_stream_requested = false;
  compass_stream_requested = false;
}

void BLEHandler::_onDisconnect(uint16_t conn_handle, uint8_t reason) {
  ble_connected = false;
  // Reset streaming flag so reconnect starts clean (BUG-1 fix)
  imu_stream_requested = false;
  compass_stream_requested = false;
}

void BLEHandler::_onWrite(uint16_t conn_handle, BLECharacteristic* chr, uint8_t* data, uint16_t len) {
  if (!data || len == 0) return;

  if (chr == &bearing_char) {
    Serial.print("[BLE] Bearing write received. Len: ");
    Serial.println(len);
    if (len == 4) {
      memcpy(&last_bearing, data, 4);
      if (callback_bearing_update) callback_bearing_update(last_bearing);
    }
  }
  else if (chr == &distance_char) {
    Serial.print("[BLE] Distance write received. Len: ");
    Serial.println(len);
    if (len == 4) {
      memcpy(&last_distance, data, 4);
      if (callback_distance_update) callback_distance_update(last_distance);
    }
  }
  else if (chr == &haptic_rx_char) {
    if (callback_haptic_rx) callback_haptic_rx();
  }
  else if (chr == &radar_mode_char) {
    radar_mode_requested = (data[0] == 0x01);
  }
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
  else if (chr == &ota_char) {
    if (data[0] == 0x01 && callback_ota_request) callback_ota_request();
  }
  else if (chr == &calib_cmd_char) {
    uint8_t cmd = data[0];
    if (cmd == 0x01 && callback_calib_start) callback_calib_start();
    else if (cmd == 0x02 && callback_calib_end) callback_calib_end();
    else if (cmd == 0x03 && callback_calib_cancel) callback_calib_cancel();
    else if (cmd == 0x04 && callback_compass_calib_start) callback_compass_calib_start();
    else if (cmd == 0x05) imu_stream_requested = true;
    else if (cmd == 0x06) imu_stream_requested = false;
    else if (cmd == 0x07) compass_stream_requested = true;
    else if (cmd == 0x08) compass_stream_requested = false;
  }
  else if (chr == &calib_threshold_char) {
    calib_threshold = data[0];
    if (callback_threshold_write) callback_threshold_write(calib_threshold);
  }
  else if (chr == &time_sync_char && len == 8) {
    uint32_t unix_ts;
    int32_t tz_offset;
    memcpy(&unix_ts, data, 4);
    memcpy(&tz_offset, data + 4, 4);
    if (callback_time_sync) callback_time_sync(unix_ts, tz_offset);
  }
}

void BLEHandler::notifyCalibStatus(uint8_t samples_done, uint8_t total_samples) {
  if (!ble_init_ok) return;
  uint8_t payload[2] = {samples_done, total_samples};
  calib_status_char.write(payload, 2);
  if (ble_connected) calib_status_char.notify(payload, 2);
}

void BLEHandler::notifyIMUStream(uint16_t mg, uint16_t dps) {
  if (!ble_init_ok || !imu_stream_requested) return;
  uint8_t payload[4] = {
    (uint8_t)(mg & 0xFF), (uint8_t)((mg >> 8) & 0xFF),
    (uint8_t)(dps & 0xFF), (uint8_t)((dps >> 8) & 0xFF)
  };
  imu_stream_char.write(payload, 4);
  if (ble_connected) imu_stream_char.notify(payload, 4);
}

void BLEHandler::notifyCalibThreshold(uint8_t threshold) {
  if (!ble_init_ok) return;
  calib_threshold_char.write(&threshold, 1);
  if (ble_connected) calib_threshold_char.notify(&threshold, 1);
  calib_threshold = threshold;
}

void BLEHandler::notifyBattery(uint8_t percent) {
  if (!ble_init_ok) return;
  battery_char.write(&percent, 1);
  if (ble_connected) battery_char.notify(&percent, 1);
}

void BLEHandler::notifyRadarModeActive(bool active) {
  if (!ble_init_ok) return;
  radar_mode_requested = active;
  uint8_t val = active ? 0x01 : 0x00;
  radar_mode_char.write(&val, 1);
  if (ble_connected) radar_mode_char.notify(&val, 1);
}

void BLEHandler::notifyHapticTX() {
  if (!ble_init_ok) return;
  uint8_t val = 0x01;
  haptic_tx_char.write(&val, 1);
  if (ble_connected) haptic_tx_char.notify(&val, 1);
}

void BLEHandler::notifyBearing(float bearing) {
  if (!ble_init_ok) return;
  bearing_char.write(&bearing, 4);
  if (ble_connected) bearing_char.notify(&bearing, 4);
}

void BLEHandler::notifyCompassStream(float heading) {
  if (!ble_init_ok) return;
  compass_stream_char.write(&heading, 4);
  if (ble_connected) compass_stream_char.notify(&heading, 4);
}

void BLEHandler::notifyDistance(uint32_t distance_m) {
  if (!ble_init_ok) return;
  distance_char.write(&distance_m, 4);
  if (ble_connected) distance_char.notify(&distance_m, 4);
}

const char* BLEHandler::getConfigJson() const {
  return config_json_buf;
}

void BLEHandler::notifyConfig(const char* json) {
  if (!ble_init_ok) return;
  if (!json) return;
  uint16_t len = strlen(json);
  if (len > sizeof(config_json_buf) - 1) len = sizeof(config_json_buf) - 1;
  memcpy(config_json_buf, json, len);
  config_json_buf[len] = '\0';
  config_char.write(config_json_buf, len);
  if (ble_connected) {
    config_char.notify(config_json_buf, len);
  }
}
