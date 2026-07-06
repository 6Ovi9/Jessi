#ifndef EEPROM_MANAGER_H
#define EEPROM_MANAGER_H

#include "config.h"
#include <Arduino.h>
#include <InternalFileSystem.h>
#include <cstring>

// ============================================================================
// EEPROM / FLASH PERSISTENCE MANAGER
// ============================================================================
// Stores calibration data (wake-on-motion threshold) in nRF52840 internal flash

struct CalibrationData {
  uint32_t timestamp;   // When calibration was last saved (4 bytes)
  uint16_t magic;       // Magic number to verify valid data (2 bytes)
  uint8_t threshold;    // WAKE_UP_THS register value (1 byte)
  uint8_t checksum;     // Simple checksum for integrity (1 byte)
};

class EEPROMManager {
public:
  EEPROMManager();
  
  // Initialize flash file system
  void begin();
  
  // Load calibration from flash
  bool loadCalibration(uint8_t& threshold);
  
  // Save calibration to flash
  bool saveCalibration(uint8_t threshold);
  
  // Get last saved timestamp
  uint32_t getLastCalibrationTime() const { return last_calib_time; }
  
  // Clear all calibration data
  bool clearCalibration();
  
  // Debug: dump flash contents
  void debugPrintFlash();

private:
  CalibrationData calib_data;
  uint32_t last_calib_time;
  bool initialized;
  
  // Helper methods
  uint8_t _calculateChecksum(const CalibrationData& data);
  bool _verifyChecksum(const CalibrationData& data);
};

#endif // EEPROM_MANAGER_H
