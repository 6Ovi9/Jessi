#include "runtime_config.h"
#include <cstring>

using namespace Adafruit_LittleFS_Namespace;

RuntimeConfigManager::RuntimeConfigManager()
  : initialized(false)
{
  memset(&config, 0, sizeof(config));
  resetDefaults();
}

void RuntimeConfigManager::begin() {
  // InternalFS is now initialized centrally in setup()
  
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
  Serial.print("  - doubleFlickWindow: "); Serial.print(config.doubleFlickWindow); Serial.println(" ms");
  
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
  config.doubleFlickWindow     = GESTURE_DOUBLE_FLICK_WINDOW_DEFAULT;
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
  int ct = _findJsonInt(json, "ct", config.clockTimeoutS);
  config.clockTimeoutS = ct < 0 ? 0 : (ct > 65535 ? 65535 : ct);
  
  int st = _findJsonInt(json, "st", config.sleepTimeoutS);
  config.sleepTimeoutS = st < 0 ? 0 : (st > 65535 ? 65535 : st);
  
  int br = _findJsonInt(json, "br", config.brightnessPercent);
  config.brightnessPercent = br < 0 ? 0 : (br > 100 ? 100 : br);
  
  int lb = _findJsonInt(json, "lb", config.lowBatteryThreshold);
  config.lowBatteryThreshold = lb < 0 ? 0 : (lb > 100 ? 100 : lb);
  
  int wt = _findJsonInt(json, "wt", config.wakeThreshold);
  config.wakeThreshold = wt < 0 ? 0 : (wt > 255 ? 255 : wt);
  
  int gt = _findJsonInt(json, "gt", config.gyroThreshold);
  config.gyroThreshold = gt < 0 ? 0 : (gt > 65535 ? 65535 : gt);
  
  int df = _findJsonInt(json, "df", config.doubleFlickWindow);
  config.doubleFlickWindow = df < 0 ? 0 : (df > 65535 ? 65535 : df);
  
  int hp = _findJsonInt(json, "hp", config.hapticPatternIndex);
  config.hapticPatternIndex = hp < 0 ? 0 : (hp > 255 ? 255 : hp);

  
  // Parse boolean (sent as 0/1)
  int lg = _findJsonInt(json, "lg", -1);
  if (lg >= 0) {
    config.logarithmicBrightness = (lg != 0);
  }
  
  saveToFlash();
  // Serial.println("[CONFIG] Updated from BLE JSON");
  return true;
}

// ============================================================================
// FLASH PERSISTENCE
// ============================================================================

bool RuntimeConfigManager::saveToFlash() {
  struct FlashData {
    RuntimeConfig cfg;
    uint16_t magic;
    uint8_t checksum;
  };
  
  FlashData data;
  // Explicitly memset to 0 to guarantee zeroed compiler padding gaps
  memset(&data, 0, sizeof(data));
  
  data.magic = RUNTIME_CONFIG_MAGIC;
  data.cfg = config;
  
  // Calculate checksum dynamically using offsetof
  data.checksum = 0;
  const uint8_t* ptr = (const uint8_t*)&data;
  uint8_t sum = 0;
  for (size_t i = 0; i < offsetof(FlashData, checksum); i++) {
    sum += ptr[i];
  }
  data.checksum = sum;
  
  // BUG-038: Write to temp file, then rename atomically.
  // Old pattern (remove then write) left no valid file on power loss mid-write.
  const char* TMP_FILE = "config.tmp";
  InternalFS.remove(TMP_FILE); // Clear any stale temp from previous crashed attempt
  
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
  
  // Atomically promote: only delete old file once the new one is safely written
  if (!InternalFS.rename(TMP_FILE, RUNTIME_CONFIG_FILE)) {
    return false;
  }
  return true;
}

bool RuntimeConfigManager::loadFromFlash() {
  struct FlashData {
    RuntimeConfig cfg;
    uint16_t magic;
    uint8_t checksum;
  };
  
  File file(InternalFS);
  if (!file.open(RUNTIME_CONFIG_FILE, FILE_O_READ)) {
    return false;
  }
  
  size_t fileSize = file.size();
  if (fileSize == 0 || fileSize > 128) {
    file.close();
    return false;
  }
  
  uint8_t buf[128];
  if (file.read(buf, fileSize) != fileSize) {
    file.close();
    return false;
  }
  file.close();

  if (fileSize == sizeof(FlashData)) {
    FlashData data;
    memcpy(&data, buf, sizeof(FlashData));
    if (data.magic != RUNTIME_CONFIG_MAGIC) return false;
    
    uint8_t sum = 0;
    for (size_t i = 0; i < offsetof(FlashData, checksum); i++) sum += buf[i];
    if (sum != data.checksum) return false;
    
    config = data.cfg;
    return true;
  }
  
  // Migration path: locate magic number to safely extract partial old config
  int magic_idx = -1;
  for (int i = fileSize - 2; i >= 0; i--) {
    uint16_t m;
    memcpy(&m, &buf[i], 2);
    if (m == RUNTIME_CONFIG_MAGIC) {
      magic_idx = i;
      break;
    }
  }
  
  if (magic_idx > 0) {
    size_t copyLen = magic_idx > sizeof(RuntimeConfig) ? sizeof(RuntimeConfig) : magic_idx;
    // config is already loaded with defaults from constructor, so we just overwrite the old fields
    memcpy(&config, buf, copyLen);
    return true;
  }
  return false;
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
  // Find "key":"value" pattern.
  // BUG-039: strstr could match a key that is a prefix of another key (e.g.
  // searching for "ct" matches "ctwo" or "cta"). Require that the character
  // immediately after the closing quote and colon delimiter is a quote.
  char search[20];
  snprintf(search, sizeof(search), "\"%s\":\"", key);
  
  const char* pos = json;
  while ((pos = strstr(pos, search)) != nullptr) {
    // Verify the key started at a JSON boundary (preceded by '{' or ',')
    if (pos > json) {
      char prev = *(pos - 1);
      if (prev != '{' && prev != ',' && prev != ' ' && prev != '\n' && prev != '\r' && prev != '\t') {
        pos++;
        continue; // False match — not a real key boundary
      }
    }
    pos += strlen(search); // Skip to value start
    uint8_t i = 0;
    while (*pos && *pos != '"' && i < maxLen - 1) {
      out[i++] = *pos++;
    }
    out[i] = '\0';
    return i > 0;
  }
  return false;
}

int RuntimeConfigManager::_findJsonInt(const char* json, const char* key, int defaultVal) {
  // Find "key":123 pattern.
  // BUG-039: Same key-boundary check as _findJsonString. Without this, key "ct"
  // could match a hypothetical future key "cta", silently reading wrong data.
  char search[20];
  snprintf(search, sizeof(search), "\"%s\":", key);
  
  const char* pos = json;
  while ((pos = strstr(pos, search)) != nullptr) {
    // Require JSON boundary before the key
    if (pos > json) {
      char prev = *(pos - 1);
      if (prev != '{' && prev != ',' && prev != ' ' && prev != '\n' && prev != '\r' && prev != '\t') {
        pos++;
        continue; // False match
      }
    }
    
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
  return defaultVal;
}
