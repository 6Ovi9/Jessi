#ifndef BLE_HANDLER_H
#define BLE_HANDLER_H

#include "config.h"
#include <Arduino.h>
#include <bluefruit.h>

// ============================================================================
// BLE HANDLER (Bluetooth Low Energy Communication)
// ============================================================================

class BLEHandler {
public:
  BLEHandler();
  
  // Initialize BLE stack and services
  void begin();
  
  // Call from main loop frequently (~10ms)
  void update();
  
  // Get connection status
  bool isConnected() const { return ble_connected; }
  
  // Send data to mobile
  void notifyBattery(uint8_t percent);
  void notifyRadarModeActive(bool active);
  void notifyHapticTX();  // User sent a touch
  void notifyBearing(float bearing);  // For debugging
  void notifyDistance(uint32_t distance_m);
  
  // Receive callbacks (set by main loop)
  typedef void (*HapticRXCallback)(void);
  typedef void (*BearingUpdateCallback)(float bearing);
  typedef void (*DistanceUpdateCallback)(uint32_t distance_m);
  typedef void (*ConfigUpdateCallback)(void);
  typedef void (*CalibStartCallback)(void);
  typedef void (*CalibEndCallback)(void);
  typedef void (*CalibCancelCallback)(void);
  
  void onHapticRX(HapticRXCallback cb) { callback_haptic_rx = cb; }
  void onBearingUpdate(BearingUpdateCallback cb) { callback_bearing_update = cb; }
  void onDistanceUpdate(DistanceUpdateCallback cb) { callback_distance_update = cb; }
  void onConfigUpdate(ConfigUpdateCallback cb) { callback_config_update = cb; }
  void onCalibStart(CalibStartCallback cb) { callback_calib_start = cb; }
  void onCalibEnd(CalibEndCallback cb) { callback_calib_end = cb; }
  void onCalibCancel(CalibCancelCallback cb) { callback_calib_cancel = cb; }

  // Time sync: app sends Unix timestamp on connect (uint32, seconds since epoch)
  typedef void (*TimeSyncCallback)(uint32_t unix_timestamp);
  void onTimeSync(TimeSyncCallback cb) { callback_time_sync = cb; }
  
  // Get latest values
  float getLastBearing() const { return last_bearing; }
  uint32_t getLastDistance() const { return last_distance; }
  bool getRadarModeRequested() const { return radar_mode_requested; }
  
  // Calibration support
  void notifyCalibStatus(uint8_t samples_done, uint8_t total_samples);
  void notifyCalibThreshold(uint8_t threshold);
  uint8_t getCalibThreshold() const { return calib_threshold; }
  
  // Config access (returns last JSON string received from app)
  const char* getConfigJson() const;
  
  // OTA (Over-The-Air Update) support
  typedef void (*OTARequestCallback)(void);
  void onOTARequest(OTARequestCallback cb) { callback_ota_request = cb; }

  // Expose these publicly so static callbacks can call them
  void _onConnect(uint16_t conn_handle);
  void _onDisconnect(uint16_t conn_handle, uint8_t reason);
  void _onWrite(uint16_t conn_handle, BLECharacteristic* chr, uint8_t* data, uint16_t len);

private:
  // BLE state
  bool ble_connected;
  
  // BLE Services and Characteristics
  BLEService service;
  
  BLECharacteristic bearing_char;
  BLECharacteristic distance_char;
  BLECharacteristic haptic_tx_char;
  BLECharacteristic haptic_rx_char;
  BLECharacteristic radar_mode_char;
  BLECharacteristic config_char;
  BLECharacteristic battery_char;
  
  BLECharacteristic calib_cmd_char;
  BLECharacteristic calib_status_char;
  BLECharacteristic calib_threshold_char;
  BLECharacteristic ota_char;
  BLECharacteristic time_sync_char;  // Write-only: app sends Unix timestamp uint32 LE
  
  // Config JSON buffer
  char config_json_buf[256];
  
  // Last received values
  float last_bearing;
  uint32_t last_distance;
  bool radar_mode_requested;
  
  // Calibration
  uint8_t calib_threshold;
  
  // Callbacks
  HapticRXCallback callback_haptic_rx;
  BearingUpdateCallback callback_bearing_update;
  DistanceUpdateCallback callback_distance_update;
  ConfigUpdateCallback callback_config_update;
  CalibStartCallback callback_calib_start;
  CalibEndCallback callback_calib_end;
  CalibCancelCallback callback_calib_cancel;
  OTARequestCallback callback_ota_request;
  void (*callback_time_sync)(uint32_t unix_timestamp);  // TimeSyncCallback
  
  static BLEHandler* instance;
  static void connect_callback(uint16_t conn_handle);
  static void disconnect_callback(uint16_t conn_handle, uint8_t reason);
  static void write_callback(uint16_t conn_handle, BLECharacteristic* chr, uint8_t* data, uint16_t len);
};

#endif // BLE_HANDLER_H
