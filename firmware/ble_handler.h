#ifndef BLE_HANDLER_H
#define BLE_HANDLER_H

#include "config.h"
#include <Arduino.h>
#include <ArduinoBLE.h>

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
  typedef void (*CalibStartCallback)(void);        // NEW
  typedef void (*CalibEndCallback)(void);          // NEW
  typedef void (*CalibCancelCallback)(void);       // NEW
  
  void onHapticRX(HapticRXCallback cb) { callback_haptic_rx = cb; }
  void onBearingUpdate(BearingUpdateCallback cb) { callback_bearing_update = cb; }
  void onDistanceUpdate(DistanceUpdateCallback cb) { callback_distance_update = cb; }
  void onConfigUpdate(ConfigUpdateCallback cb) { callback_config_update = cb; }
  void onCalibStart(CalibStartCallback cb) { callback_calib_start = cb; }     // NEW
  void onCalibEnd(CalibEndCallback cb) { callback_calib_end = cb; }           // NEW
  void onCalibCancel(CalibCancelCallback cb) { callback_calib_cancel = cb; }  // NEW
  
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

private:
  // BLE state
  bool ble_connected;
  uint32_t last_connection_check_ms;
  
  // BLE Services and Characteristics
  BLEService service;
  
  BLEFloatCharacteristic bearing_char;
  BLEUnsignedLongCharacteristic distance_char;
  BLEByteCharacteristic haptic_tx_char;
  BLEByteCharacteristic haptic_rx_char;
  BLEByteCharacteristic radar_mode_char;
  BLEStringCharacteristic config_char;
  BLEByteCharacteristic battery_char;
  
  BLEByteCharacteristic calib_cmd_char;         // NEW: calibration command
  BLEByteCharacteristic calib_status_char;      // NEW: calibration progress
  BLEByteCharacteristic calib_threshold_char;   // NEW: wake-on-motion threshold
  BLEByteCharacteristic ota_char;               // OTA trigger (write 0x01)
  
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
  CalibStartCallback callback_calib_start;      // NEW
  CalibEndCallback callback_calib_end;          // NEW
  CalibCancelCallback callback_calib_cancel;    // NEW
  OTARequestCallback callback_ota_request;      // OTA
  
  // Helper methods
  void _setupServices();
  void _checkConnectionStatus();
  void _handleCharacteristicUpdates();
};

#endif // BLE_HANDLER_H
