#include "ble_handler.h"

// UUIDs (16-bit shorthand; ArduinoBLE expands to 128-bit automatically)
// IMPORTANT: ArduinoBLE expects bare hex strings WITHOUT "0x" prefix
#define BLE_SERVICE_UUID          "180A"
#define BLE_BEARING_UUID          "2A58"
#define BLE_DISTANCE_UUID         "2A59"
#define BLE_HAPTIC_TX_UUID        "2A5A"
#define BLE_HAPTIC_RX_UUID        "2A5B"
#define BLE_BATTERY_UUID          "2A19"  // Standard battery service
#define BLE_RADAR_MODE_UUID       "2A5F"  // Radar mode active (notify)
#define BLE_CONFIG_UUID           "2A60"  // Config JSON (read+write)
#define BLE_CALIB_CMD_UUID        "2A5C"
#define BLE_CALIB_STATUS_UUID     "2A5D"
#define BLE_CALIB_THRESHOLD_UUID  "2A5E"
#define BLE_OTA_UUID              "2A61"  // OTA trigger (write 0x01 to enter DFU)

BLEHandler::BLEHandler()
  : ble_connected(false),
    last_connection_check_ms(0),
    service(BLE_SERVICE_UUID),
    bearing_char(BLE_BEARING_UUID, BLERead | BLEWrite | BLENotify),
    distance_char(BLE_DISTANCE_UUID, BLERead | BLEWrite | BLENotify),
    haptic_tx_char(BLE_HAPTIC_TX_UUID, BLERead | BLENotify),
    haptic_rx_char(BLE_HAPTIC_RX_UUID, BLERead | BLEWrite),
    radar_mode_char(BLE_RADAR_MODE_UUID, BLERead | BLENotify),
    config_char(BLE_CONFIG_UUID, BLERead | BLEWrite),
    battery_char(BLE_BATTERY_UUID, BLERead | BLENotify),
    calib_cmd_char(BLE_CALIB_CMD_UUID, BLERead | BLEWrite),
    calib_status_char(BLE_CALIB_STATUS_UUID, BLERead | BLENotify),
    calib_threshold_char(BLE_CALIB_THRESHOLD_UUID, BLERead | BLEWrite),
    ota_char(BLE_OTA_UUID, BLERead | BLEWrite),
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
    callback_ota_request(nullptr)
{
}

void BLEHandler::begin() {
  // Initialize BLE
  if (!BLE.begin()) {
    // Serial.println("[BLE] Failed to initialize BLE!");
    return;
  }
  
  // Set device name and advertised service
  BLE.setLocalName(BLE_DEVICE_NAME);
  BLE.setAdvertisedService(service);
  
  // Add characteristics to service
  service.addCharacteristic(bearing_char);
  service.addCharacteristic(distance_char);
  service.addCharacteristic(haptic_tx_char);
  service.addCharacteristic(haptic_rx_char);
  service.addCharacteristic(radar_mode_char);
  service.addCharacteristic(config_char);
  service.addCharacteristic(battery_char);
  service.addCharacteristic(calib_cmd_char);
  service.addCharacteristic(calib_status_char);
  service.addCharacteristic(calib_threshold_char);
  service.addCharacteristic(ota_char);
  
  // Add service to BLE
  BLE.addService(service);
  
  // Set initial values
  bearing_char.writeValue(0.0f);
  distance_char.writeValue(0UL);
  battery_char.writeValue(100);
  
  // Start advertising
  BLE.advertise();
  
  // Serial.println("[BLE] Initialized and advertising");
}

void BLEHandler::update() {
  // Check for new BLE connections
  _checkConnectionStatus();
  
  // Handle incoming characteristic writes
  _handleCharacteristicUpdates();
}

void BLEHandler::_checkConnectionStatus() {
  uint32_t now_ms = millis();
  
  // Update connection status every 1 second
  if ((now_ms - last_connection_check_ms) >= 1000) {
    last_connection_check_ms = now_ms;
    
    BLEDevice central = BLE.central();
    
    if (central) {
      ble_connected = true;
      // Serial.print("[BLE] Connected to: ");
      // Serial.println(central.address());
    } else {
      ble_connected = false;
      // Serial.println("[BLE] Disconnected");
    }
  }
}

void BLEHandler::_handleCharacteristicUpdates() {
  // Check for bearing update
  if (bearing_char.written()) {
    last_bearing = bearing_char.value();
    if (callback_bearing_update) {
      callback_bearing_update(last_bearing);
    }
  }
  
  // Check for distance update
  if (distance_char.written()) {
    last_distance = distance_char.value();
    if (callback_distance_update) {
      callback_distance_update(last_distance);
    }
  }
  
  // Check for haptic RX
  if (haptic_rx_char.written()) {
    if (callback_haptic_rx) {
      callback_haptic_rx();
    }
  }
  
  // Check for radar mode request
  if (radar_mode_char.written()) {
    uint8_t value = radar_mode_char.value();
    radar_mode_requested = (value == 0x01);
  }
  
  // Check for config update — store JSON in buffer for main loop to read
  if (config_char.written()) {
    String val = config_char.value();
    uint8_t len = val.length();
    if (len > 0 && len < sizeof(config_json_buf)) {
      memcpy(config_json_buf, val.c_str(), len);
      config_json_buf[len] = '\0';
    }
    if (callback_config_update) {
      callback_config_update();
    }
  }
  
  // Check for OTA trigger
  if (ota_char.written()) {
    uint8_t cmd = ota_char.value();
    if (cmd == 0x01 && callback_ota_request) {
      callback_ota_request();
    }
  }
  
  // Check for calibration command
  if (calib_cmd_char.written()) {
    uint8_t cmd = calib_cmd_char.value();
    if (cmd == 0x01 && callback_calib_start) {       // START
      callback_calib_start();
    } else if (cmd == 0x02 && callback_calib_end) {  // END
      callback_calib_end();
    } else if (cmd == 0x03 && callback_calib_cancel) { // CANCEL
      callback_calib_cancel();
    }
  }
  
  // Check for calibration threshold write (update from app)
  if (calib_threshold_char.written()) {
    calib_threshold = calib_threshold_char.value();
  }
}

void BLEHandler::notifyCalibStatus(uint8_t samples_done, uint8_t total_samples) {
  uint8_t progress = (samples_done * 255) / total_samples;
  calib_status_char.writeValue(progress);
}

void BLEHandler::notifyCalibThreshold(uint8_t threshold) {
  calib_threshold_char.writeValue(threshold);
  calib_threshold = threshold;
}

void BLEHandler::notifyBattery(uint8_t percent) {
  battery_char.writeValue(percent);
}

void BLEHandler::notifyRadarModeActive(bool active) {
  radar_mode_char.writeValue(active ? 0x01 : 0x00);
}

void BLEHandler::notifyHapticTX() {
  haptic_tx_char.writeValue(0x01);
}

void BLEHandler::notifyBearing(float bearing) {
  bearing_char.writeValue(bearing);
}

void BLEHandler::notifyDistance(uint32_t distance_m) {
  distance_char.writeValue(distance_m);
}

const char* BLEHandler::getConfigJson() const {
  return config_json_buf;
}
