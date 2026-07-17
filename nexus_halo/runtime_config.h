#ifndef RUNTIME_CONFIG_H
#define RUNTIME_CONFIG_H

#include "config.h"
#include <Arduino.h>
#include <InternalFileSystem.h>

// ============================================================================
// RUNTIME CONFIGURATION (received from app via BLE, persisted in flash)
// ============================================================================
// This replaces the hardcoded #define colors when the user customizes via app.

#define RUNTIME_CONFIG_MAGIC  0xCF7F  // "CF" = config (v7) — bumped: uint8_t→uint16_t for timeout fields
#define RUNTIME_CONFIG_FILE   "config.dat"

struct __attribute__((packed)) RuntimeConfig {
  // 32-bit fields
  uint32_t colorHoursConnected;
  uint32_t colorMinutesConnected;
  uint32_t colorSecondsConnected;
  uint32_t colorHoursDisc;
  uint32_t colorMinutesDisc;
  uint32_t colorSecondsDisc;
  
  // 16-bit fields
  uint16_t gyroThreshold;
  uint16_t doubleFlickWindow;
  
  // 8-bit/bool fields
  uint8_t  brightnessPercent;
  uint16_t clockTimeoutS;   // was uint8_t — max 255s caused wrap on timeouts > 4min from app
  uint16_t sleepTimeoutS;   // was uint8_t — same fix
  uint8_t  lowBatteryThreshold;
  uint8_t  hapticPatternIndex;
  uint8_t  wakeThreshold;
  bool     logarithmicBrightness;
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
  
  void setWakeThreshold(uint8_t ths) {
    config.wakeThreshold = ths;
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
