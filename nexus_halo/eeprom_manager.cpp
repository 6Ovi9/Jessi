#include "eeprom_manager.h"

using namespace Adafruit_LittleFS_Namespace;

EEPROMManager::EEPROMManager()
  : last_calib_time(0),
    initialized(false)
{
  memset(&calib_data, 0, sizeof(calib_data));
}

void EEPROMManager::begin() {
  // Initialize internal file system (nRF52840 flash)
  // InternalFS is now initialized centrally in setup()
  initialized = true;
  
  // Try to load existing calibration
  loadCalibration(calib_data.threshold);
  
  // Serial.println("[EEPROM] Manager initialized");
}

bool EEPROMManager::loadCalibration(uint8_t& threshold) {
  if (!initialized) {
    return false;
  }
  
  File file(InternalFS);
  if (!file.open("calib.dat", FILE_O_READ)) {
    // Serial.println("[EEPROM] No calibration file found");
    threshold = IMU_WAKE_UP_THS_DEFAULT;
    return false;
  }
  
  // Read calibration data
  CalibrationData data;
  if (file.read(&data, sizeof(data)) != sizeof(data)) {
    file.close();
    // Serial.println("[EEPROM] Failed to read calibration data");
    threshold = IMU_WAKE_UP_THS_DEFAULT;
    return false;
  }
  file.close();
  
  // Verify magic number
  if (data.magic != EEPROM_CALIB_MAGIC) {
    // Serial.println("[EEPROM] Invalid magic number");
    threshold = IMU_WAKE_UP_THS_DEFAULT;
    return false;
  }
  
  // Verify checksum
  if (!_verifyChecksum(data)) {
    // Serial.println("[EEPROM] Checksum failed");
    threshold = IMU_WAKE_UP_THS_DEFAULT;
    return false;
  }
  
  threshold = data.threshold;
  last_calib_time = data.timestamp;
  calib_data = data;
  
  // Serial.print("[EEPROM] Calibration loaded: threshold=0x");
  // Serial.print(data.threshold, HEX);
  // Serial.print(", timestamp=");
  // Serial.println(data.timestamp);
  
  return true;
}

bool EEPROMManager::saveCalibration(uint8_t threshold) {
  if (!initialized) {
    return false;
  }
  
  // Prepare data - explicitly memset to 0 to eliminate padding gaps
  CalibrationData data;
  memset(&data, 0, sizeof(data));
  
  data.magic = EEPROM_CALIB_MAGIC;
  data.threshold = threshold;
  data.timestamp = last_calib_time + 1;  // Update sequence counter (Note: downstream logic must not treat this as wall-clock time)
  data.checksum = 0;
  data.checksum = _calculateChecksum(data);
  
  // BUG-038: Write to temp file, then rename atomically.
  const char* TMP_FILE = "calib.tmp";
  InternalFS.remove(TMP_FILE);
  
  File file(InternalFS);
  if (!file.open(TMP_FILE, FILE_O_WRITE)) {
    return false;
  }
  
  if (file.write((const uint8_t*)&data, sizeof(data)) != sizeof(data)) {
    file.close();
    InternalFS.remove(TMP_FILE);
    return false;
  }
  file.close();
  
  if (!InternalFS.rename(TMP_FILE, "calib.dat")) {
    return false;
  }
  
  calib_data = data;
  last_calib_time = data.timestamp;
  
  // Serial.print("[EEPROM] Calibration saved: threshold=0x");
  // Serial.print(data.threshold, HEX);
  // Serial.print(", timestamp=");
  // Serial.println(data.timestamp);
  
  return true;
}

bool EEPROMManager::clearCalibration() {
  if (!initialized) {
    return false;
  }
  
  InternalFS.remove("calib.dat");
  memset(&calib_data, 0, sizeof(calib_data));
  last_calib_time = 0;
  
  // Serial.println("[EEPROM] Calibration cleared");
  return true;
}

uint8_t EEPROMManager::_calculateChecksum(const CalibrationData& data) {
  uint8_t sum = 0;
  const uint8_t* ptr = (const uint8_t*)&data;
  
  // Sum all bytes except checksum field using offsetof
  for (size_t i = 0; i < offsetof(CalibrationData, checksum); i++) {
    sum ^= ptr[i];
  }
  
  return sum;
}

bool EEPROMManager::_verifyChecksum(const CalibrationData& data) {
  uint8_t calculated = _calculateChecksum(data);
  return (calculated == data.checksum);
}

void EEPROMManager::debugPrintFlash() {
  // Serial.println("[EEPROM] Current data in memory:");
  // Serial.print("  Magic: 0x");
  // Serial.println(calib_data.magic, HEX);
  // Serial.print("  Threshold: 0x");
  // Serial.println(calib_data.threshold, HEX);
  // Serial.print("  Timestamp: ");
  // Serial.println(calib_data.timestamp);
  // Serial.print("  Checksum: 0x");
  // Serial.println(calib_data.checksum, HEX);
}
