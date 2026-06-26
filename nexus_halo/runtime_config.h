#ifndef RUNTIME_CONFIG_H
#define RUNTIME_CONFIG_H

#include "config.h"
#include <Arduino.h>
#include <InternalFileSystem.h>

// ============================================================================
// RUNTIME CONFIGURATION (received from app via BLE, persisted in flash)
// ============================================================================
// This replaces the hardcoded #define colors when the user customizes via app.

#define RUNTIME_CONFIG_MAGIC  0xCF7E  // "CF" = config (v6)
#define RUNTIME_CONFIG_FILE   "config.dat"

struct RuntimeConfig {
  // Colors: CLOCK_CONNECTED
  uint32_t colorHoursConnected;
  uint32_t colorMinutesConnected;
  uint32_t colorSecondsConnected;
  
  // Colors: CLOCK_DISCONNECTED
  uint32_t colorHoursDisc;
  uint32_t colorMinutesDisc;
  uint32_t colorSecondsDisc;
  
  // Brightness & display
  uint8_t  brightnessPercent;
  bool     logarithmicBrightness;
  
  // Timers
  uint8_t  clockTimeoutS;
  uint8_t  sleepTimeoutS;
  
  // Battery
  uint8_t  lowBatteryThreshold;
  
  // Haptic pattern (simplified to index)
  uint8_t  hapticPatternIndex;
  
  // IMU wake threshold
  uint8_t  wakeThreshold;

  // Gyroscope wrist-flick threshold (dps)
  uint16_t gyroThreshold;
};

class RuntimeConfigManager {
public:
  RuntimeConfigManager();
  
  // Initialize: load from flash or use defaults
  void begin();
  
  // Get current config (always valid — defaults if not loaded)
  const RuntimeConfig& getConfig() const { return config; }
  
  // Set brightness and save to flash
  void setBrightnessPercent(uint8_t pct) {
    config.brightnessPercent = pct;
    saveToFlash();
  }
  
  // Update config from BLE JSON string
  // Returns true if successfully parsed and saved
  bool updateFromJson(const char* json);
  
  // Save current config to flash
  bool saveToFlash();
  
  // Load config from flash (called by begin())
  bool loadFromFlash();
  
  // Reset to factory defaults
  void resetDefaults();

private:
  RuntimeConfig config;
  bool initialized;
  
  // Parse helpers
  static uint32_t _parseHexColor(const char* hex, uint8_t len);
  static bool _findJsonString(const char* json, const char* key, char* out, uint8_t maxLen);
  static int  _findJsonInt(const char* json, const char* key, int defaultVal);
};

#endif // RUNTIME_CONFIG_H
