#include "runtime_config.h"
#include <cstring>

using namespace Adafruit_LittleFS_Namespace;

RuntimeConfigManager::RuntimeConfigManager()
  : initialized(false)
{
  resetDefaults();
}

void RuntimeConfigManager::begin() {
  InternalFS.begin();
  
  if (!loadFromFlash()) {
    resetDefaults();
    Serial.println("[CONFIG] No saved config found, using DEFAULTS:");
  } else {
    Serial.println("[CONFIG] Loaded saved config from flash:");
  }
  
  Serial.print("  - colorHoursConnected: 0x"); Serial.println(config.colorHoursConnected, HEX);
  Serial.print("  - colorMinutesConnected: 0x"); Serial.println(config.colorMinutesConnected, HEX);
  Serial.print("  - colorSecondsConnected: 0x"); Serial.println(config.colorSecondsConnected, HEX);
  Serial.print("  - colorHoursDisc: 0x"); Serial.println(config.colorHoursDisc, HEX);
  Serial.print("  - colorMinutesDisc: 0x"); Serial.println(config.colorMinutesDisc, HEX);
  Serial.print("  - colorSecondsDisc: 0x"); Serial.println(config.colorSecondsDisc, HEX);
  Serial.print("  - brightnessPercent: "); Serial.print(config.brightnessPercent); Serial.println("%");
  Serial.print("  - logarithmicBrightness: "); Serial.println(config.logarithmicBrightness);
  Serial.print("  - wakeThreshold: "); Serial.println(config.wakeThreshold);
  
  initialized = true;
}

void RuntimeConfigManager::resetDefaults() {
  config.colorHoursConnected   = COLOR_HOURS_CONNECTED;
  config.colorMinutesConnected = COLOR_MINUTES_CONNECTED;
  config.colorSecondsConnected = COLOR_SECONDS_CONNECTED;
  config.colorHoursDisc        = COLOR_HOURS_DISC;
  config.colorMinutesDisc      = COLOR_MINUTES_DISC;
  config.colorSecondsDisc      = COLOR_SECONDS_DISC;
  config.brightnessPercent     = LED_CLOCK_BRIGHTNESS;
  config.logarithmicBrightness = true;
  config.clockTimeoutS         = TIMER_CLOCK_TIMEOUT_MS / 1000;
  config.sleepTimeoutS         = TIMER_RADAR_TIMEOUT_MS / 1000;
  config.lowBatteryThreshold   = LOW_BATTERY_THRESHOLD_PERCENT;
  config.hapticPatternIndex    = 0;
  config.wakeThreshold         = IMU_WAKE_UP_THS_DEFAULT;
  config.gyroThreshold         = GESTURE_GYRO_THS_DEFAULT;
}

// ============================================================================
// JSON PARSING (lightweight, no ArduinoJson dependency)
// ============================================================================
// Expected format from app: {"ct":5,"st":5,"chc":"FFFFDCB4","cmc":"FFFFF5F0",...}

bool RuntimeConfigManager::updateFromJson(const char* json) {
  if (!json || json[0] == '\0') return false;
  
  char buf[12];
  
  // Parse colors (hex strings like "FFFFDCB4")
  if (_findJsonString(json, "chc", buf, sizeof(buf))) {
    config.colorHoursConnected = _parseHexColor(buf, strlen(buf));
  }
  if (_findJsonString(json, "cmc", buf, sizeof(buf))) {
    config.colorMinutesConnected = _parseHexColor(buf, strlen(buf));
  }
  if (_findJsonString(json, "csc", buf, sizeof(buf))) {
    config.colorSecondsConnected = _parseHexColor(buf, strlen(buf));
  }
  if (_findJsonString(json, "chd", buf, sizeof(buf))) {
    config.colorHoursDisc = _parseHexColor(buf, strlen(buf));
  }
  if (_findJsonString(json, "cmd", buf, sizeof(buf))) {
    config.colorMinutesDisc = _parseHexColor(buf, strlen(buf));
  }
  if (_findJsonString(json, "csd", buf, sizeof(buf))) {
    config.colorSecondsDisc = _parseHexColor(buf, strlen(buf));
  }
  
  // Parse integers
  config.clockTimeoutS     = _findJsonInt(json, "ct", config.clockTimeoutS);
  config.sleepTimeoutS     = _findJsonInt(json, "st", config.sleepTimeoutS);
  config.brightnessPercent = _findJsonInt(json, "br", config.brightnessPercent);
  config.lowBatteryThreshold = _findJsonInt(json, "lb", config.lowBatteryThreshold);
  config.wakeThreshold     = _findJsonInt(json, "wt", config.wakeThreshold);
  config.gyroThreshold     = _findJsonInt(json, "gt", config.gyroThreshold);
  config.hapticPatternIndex = _findJsonInt(json, "hp", config.hapticPatternIndex);

  
  // Parse boolean (sent as 0/1)
  int lg = _findJsonInt(json, "lg", -1);
  if (lg >= 0) {
    config.logarithmicBrightness = (lg != 0);
  }
  
  // Serial.println("[CONFIG] Updated from BLE JSON");
  return true;
}

// ============================================================================
// FLASH PERSISTENCE
// ============================================================================

bool RuntimeConfigManager::saveToFlash() {
  struct FlashData {
    uint16_t magic;
    RuntimeConfig cfg;
    uint8_t checksum;
  };
  
  FlashData data;
  data.magic = RUNTIME_CONFIG_MAGIC;
  data.cfg = config;
  
  // Calculate checksum
  data.checksum = 0;
  const uint8_t* ptr = (const uint8_t*)&data;
  uint8_t sum = 0;
  for (size_t i = 0; i < sizeof(data) - 1; i++) {
    sum += ptr[i];
  }
  data.checksum = sum;
  
  // Delete old file first (InternalFS doesn't support overwrite well)
  InternalFS.remove(RUNTIME_CONFIG_FILE);
  
  File file(InternalFS);
  if (!file.open(RUNTIME_CONFIG_FILE, FILE_O_WRITE)) {
    return false;
  }
  
  file.write((const uint8_t*)&data, sizeof(data));
  file.close();
  return true;
}

bool RuntimeConfigManager::loadFromFlash() {
  struct FlashData {
    uint16_t magic;
    RuntimeConfig cfg;
    uint8_t checksum;
  };
  
  File file(InternalFS);
  if (!file.open(RUNTIME_CONFIG_FILE, FILE_O_READ)) {
    return false;
  }
  
  FlashData data;
  if (file.read(&data, sizeof(data)) != sizeof(data)) {
    file.close();
    return false;
  }
  file.close();
  
  // Verify magic
  if (data.magic != RUNTIME_CONFIG_MAGIC) {
    return false;
  }
  
  // Verify checksum
  const uint8_t* ptr = (const uint8_t*)&data;
  uint8_t sum = 0;
  for (size_t i = 0; i < sizeof(data) - 1; i++) {
    sum += ptr[i];
  }
  if (sum != data.checksum) {
    return false;
  }
  
  config = data.cfg;
  // Serial.println("[CONFIG] Loaded from flash");
  return true;
}

// ============================================================================
// JSON PARSE HELPERS (minimal, no heap allocation)
// ============================================================================

uint32_t RuntimeConfigManager::_parseHexColor(const char* hex, uint8_t len) {
  uint32_t result = 0;
  for (uint8_t i = 0; i < len && i < 8; i++) {
    result <<= 4;
    char c = hex[i];
    if (c >= '0' && c <= '9') result |= (c - '0');
    else if (c >= 'A' && c <= 'F') result |= (c - 'A' + 10);
    else if (c >= 'a' && c <= 'f') result |= (c - 'a' + 10);
  }
  return result;
}

bool RuntimeConfigManager::_findJsonString(const char* json, const char* key, char* out, uint8_t maxLen) {
  // Find "key":"value" pattern
  char search[20];
  snprintf(search, sizeof(search), "\"%s\":\"", key);
  
  const char* pos = strstr(json, search);
  if (!pos) return false;
  
  pos += strlen(search);  // Skip to value start
  
  uint8_t i = 0;
  while (*pos && *pos != '"' && i < maxLen - 1) {
    out[i++] = *pos++;
  }
  out[i] = '\0';
  return i > 0;
}

int RuntimeConfigManager::_findJsonInt(const char* json, const char* key, int defaultVal) {
  // Find "key":123 pattern
  char search[20];
  snprintf(search, sizeof(search), "\"%s\":", key);
  
  const char* pos = strstr(json, search);
  if (!pos) return defaultVal;
  
  pos += strlen(search);
  
  // Skip whitespace
  while (*pos == ' ') pos++;
  
  // Parse integer
  bool negative = false;
  if (*pos == '-') { negative = true; pos++; }
  
  int result = 0;
  bool found = false;
  while (*pos >= '0' && *pos <= '9') {
    result = result * 10 + (*pos - '0');
    pos++;
    found = true;
  }
  
  if (!found) return defaultVal;
  return negative ? -result : result;
}
