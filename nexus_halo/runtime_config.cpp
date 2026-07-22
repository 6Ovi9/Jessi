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
  Serial.print("  - brightnessHapticTx: "); Serial.print(config.brightnessHapticTx); Serial.println("%");
  Serial.print("  - brightnessHapticRx: "); Serial.print(config.brightnessHapticRx); Serial.println("%");
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
  config.colorHapticTx         = COLOR_HAPTIC_TX;
  config.colorHapticRx         = COLOR_HAPTIC_RX;
  config.brightnessPercent     = LED_CLOCK_BRIGHTNESS;
  config.brightnessHapticTx    = 100;
  config.brightnessHapticRx    = 100;
  config.logarithmicBrightness = true;
  config.clockTimeoutS         = TIMER_CLOCK_TIMEOUT_MS / 1000;
  config.sleepTimeoutS         = TIMER_RADAR_TIMEOUT_MS / 1000;
  config.lowBatteryThreshold   = LOW_BATTERY_THRESHOLD_PERCENT;
  config.hapticPatternIndex    = 0;
  config.wakeThreshold         = IMU_WAKE_UP_THS_DEFAULT;
  config.gyroThreshold         = GESTURE_GYRO_THS_DEFAULT;
  config.doubleFlickWindow     = GESTURE_DOUBLE_FLICK_WINDOW_DEFAULT;
  config.tripleFlickWindowMs   = GESTURE_TRIPLE_FLICK_WINDOW_DEFAULT;
  // Radar / distance colors & thresholds
  config.colorRadar            = COLOR_RADAR;
  config.colorDistanceNear     = COLOR_DISTANCE_NEAR;
  config.colorDistanceProv     = COLOR_DISTANCE_PROVINCE;
  config.colorDistanceFar      = COLOR_DISTANCE_FAR;
  config.colorDistanceVFar     = COLOR_DISTANCE_VFAR;
  config.colorDistanceExtr     = COLOR_DISTANCE_EXTREME;
  config.distThresh1Km         = DISTANCE_THRESHOLD_1_KM;
  config.distThresh2Km         = DISTANCE_THRESHOLD_2_KM;
  config.distThresh3Km         = DISTANCE_THRESHOLD_3_KM;
  config.distThresh4Km         = DISTANCE_THRESHOLD_4_KM;
  config.distThreshMaxKm       = DISTANCE_THRESHOLD_MAX_KM;
  config.ledsDistanceNear      = 3;
  config.ledsDistanceProv      = 3;
  config.ledsDistanceFar       = 2;
  config.ledsDistanceVFar      = 2;
  config.ledsDistanceExtr      = 2;
  config.colorFlickFeedback     = COLOR_FLICK_FEEDBACK;
  config.brightnessFlickFeedback= BRIGHTNESS_FLICK_FEEDBACK;
  config.enableFlickFeedback    = ENABLE_FLICK_FEEDBACK;
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
  if (_findJsonString(json, "ctx", buf, sizeof(buf))) {
    config.colorHapticTx = _parseHexColor(buf, strlen(buf));
  }
  if (_findJsonString(json, "crx", buf, sizeof(buf))) {
    config.colorHapticRx = _parseHexColor(buf, strlen(buf));
  }
  
  // Parse integers
  int ct = _findJsonInt(json, "ct", config.clockTimeoutS);
  config.clockTimeoutS = ct < 0 ? 0 : (ct > 65535 ? 65535 : ct);
  
  int st = _findJsonInt(json, "st", config.sleepTimeoutS);
  config.sleepTimeoutS = st < 0 ? 0 : (st > 65535 ? 65535 : st);
  
  int br = _findJsonInt(json, "br", config.brightnessPercent);
  config.brightnessPercent = br < 0 ? 0 : (br > 100 ? 100 : br);
  
  int btx = _findJsonInt(json, "btx", config.brightnessHapticTx);
  config.brightnessHapticTx = btx < 0 ? 0 : (btx > 100 ? 100 : btx);
  
  int brx = _findJsonInt(json, "brx", config.brightnessHapticRx);
  config.brightnessHapticRx = brx < 0 ? 0 : (brx > 100 ? 100 : brx);
  
  int lb = _findJsonInt(json, "lb", config.lowBatteryThreshold);
  config.lowBatteryThreshold = lb < 0 ? 0 : (lb > 100 ? 100 : lb);
  
  int wt = _findJsonInt(json, "wt", config.wakeThreshold);
  config.wakeThreshold = wt < 0 ? 0 : (wt > 255 ? 255 : wt);
  
  int gt = _findJsonInt(json, "gt", config.gyroThreshold);
  config.gyroThreshold = gt < 0 ? 0 : (gt > 65535 ? 65535 : gt);
  
  int df = _findJsonInt(json, "df", config.doubleFlickWindow);
  config.doubleFlickWindow = df < 0 ? 0 : (df > 65535 ? 65535 : df);

  int tf = _findJsonInt(json, "tf", config.tripleFlickWindowMs);
  config.tripleFlickWindowMs = tf < 0 ? 0 : (tf > 65535 ? 65535 : tf);
  
  int hp = _findJsonInt(json, "hp", config.hapticPatternIndex);
  config.hapticPatternIndex = hp < 0 ? 0 : (hp > 255 ? 255 : hp);

  
  // Parse boolean (sent as 0/1)
  int lg = _findJsonInt(json, "lg", -1);
  if (lg >= 0) {
    config.logarithmicBrightness = (lg != 0);
  }

  // ── Radar / distance colors ──────────────────────────────────────────────
  if (_findJsonString(json, "cra", buf, sizeof(buf))) {
    config.colorRadar = _parseHexColor(buf, strlen(buf));
  }
  if (_findJsonString(json, "cdn", buf, sizeof(buf))) {
    config.colorDistanceNear = _parseHexColor(buf, strlen(buf));
  }
  if (_findJsonString(json, "cdp", buf, sizeof(buf))) {
    config.colorDistanceProv = _parseHexColor(buf, strlen(buf));
  }
  if (_findJsonString(json, "cdf", buf, sizeof(buf))) {
    config.colorDistanceFar = _parseHexColor(buf, strlen(buf));
  }
  if (_findJsonString(json, "cdv", buf, sizeof(buf))) {
    config.colorDistanceVFar = _parseHexColor(buf, strlen(buf));
  }
  if (_findJsonString(json, "cde", buf, sizeof(buf))) {
    config.colorDistanceExtr = _parseHexColor(buf, strlen(buf));
  }

  // ── Distance thresholds (km) ─────────────────────────────────────────────
  int dt1 = _findJsonInt(json, "dt1", config.distThresh1Km);
  config.distThresh1Km = (dt1 < 1) ? 1 : (dt1 > 9999 ? 9999 : dt1);

  int dt2 = _findJsonInt(json, "dt2", config.distThresh2Km);
  dt2 = (dt2 < 1) ? 1 : (dt2 > 9999 ? 9999 : dt2);
  config.distThresh2Km = (dt2 > config.distThresh1Km) ? dt2 : config.distThresh1Km + 1;

  int dt3 = _findJsonInt(json, "dt3", config.distThresh3Km);
  dt3 = (dt3 < 1) ? 1 : (dt3 > 9999 ? 9999 : dt3);
  config.distThresh3Km = (dt3 > config.distThresh2Km) ? dt3 : config.distThresh2Km + 1;

  int dt4 = _findJsonInt(json, "dt4", config.distThresh4Km);
  dt4 = (dt4 < 1) ? 1 : (dt4 > 9999 ? 9999 : dt4);
  config.distThresh4Km = (dt4 > config.distThresh3Km) ? dt4 : config.distThresh3Km + 1;

  int dtm = _findJsonInt(json, "dtm", config.distThreshMaxKm);
  dtm = (dtm < 2) ? 2 : (dtm > 9999 ? 9999 : dtm);
  config.distThreshMaxKm = (dtm > config.distThresh4Km) ? dtm : config.distThresh4Km + 1;

  // ── Distance zone LED counts ─────────────────────────────────────────────
  int ln = _findJsonInt(json, "ln", config.ledsDistanceNear);
  config.ledsDistanceNear = (ln < 0) ? 0 : (ln > 12 ? 12 : ln);

  int lp = _findJsonInt(json, "lp", config.ledsDistanceProv);
  config.ledsDistanceProv = (lp < 0) ? 0 : (lp > 12 ? 12 : lp);

  int lf = _findJsonInt(json, "lf", config.ledsDistanceFar);
  config.ledsDistanceFar = (lf < 0) ? 0 : (lf > 12 ? 12 : lf);

  int lv = _findJsonInt(json, "lv", config.ledsDistanceVFar);
  config.ledsDistanceVFar = (lv < 0) ? 0 : (lv > 12 ? 12 : lv);

  int le = _findJsonInt(json, "le", config.ledsDistanceExtr);
  config.ledsDistanceExtr = (le < 0) ? 0 : (le > 12 ? 12 : le);

  // Validate total is exactly 12. If not, fallback to 3-3-2-2-2 to prevent visual bugs.
  int total_leds = config.ledsDistanceNear + config.ledsDistanceProv + config.ledsDistanceFar + config.ledsDistanceVFar + config.ledsDistanceExtr;
  if (total_leds != LED_COUNT) {
    config.ledsDistanceNear = 3;
    config.ledsDistanceProv = 3;
    config.ledsDistanceFar  = 2;
    config.ledsDistanceVFar = 2;
    config.ledsDistanceExtr = 2;
  }

  // ── Flick Feedback Customization ─────────────────────────────────────────
  if (_findJsonString(json, "cff", buf, sizeof(buf))) {
    config.colorFlickFeedback = _parseHexColor(buf, strlen(buf));
  }
  int bff = _findJsonInt(json, "bff", config.brightnessFlickFeedback);
  config.brightnessFlickFeedback = bff < 10 ? 10 : (bff > 100 ? 100 : bff);

  int eff = _findJsonInt(json, "eff", -1);
  if (eff >= 0) {
    config.enableFlickFeedback = (eff != 0);
  }

  // Deferred saveToFlash() managed by config_save_pending in nexus_halo.ino loop
  // saveToFlash();
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
  if (fileSize == 0 || fileSize > 256) {
    file.close();
    return false;
  }
  
  uint8_t buf[256]; // Increased from 128 to accommodate larger RuntimeConfig struct
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
    return false; // Fall back to factory defaults instead of corrupting data
  }
  return false;
}

// ============================================================================
// JSON PARSE HELPERS (minimal, no heap allocation)
// ============================================================================

uint32_t RuntimeConfigManager::_parseHexColor(const char* hex, uint8_t len) {
  if (!hex || len == 0) return 0;

  if (hex[0] == '#') {
    hex++;
    len--;
  }
  
  // If 6-character hex string ("RRGGBB"), prepend "FF" alpha channel (0xFFRRGGBB)
  char fullHex[9];
  if (len == 6) {
    fullHex[0] = 'F';
    fullHex[1] = 'F';
    memcpy(fullHex + 2, hex, 6);
    fullHex[8] = '\0';
    hex = fullHex;
    len = 8;
  }

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
      int digit = *pos - '0';
      if (result > (2147483647 - digit) / 10) {
        result = 2147483647;
        break;
      }
      result = result * 10 + digit;
      pos++;
      found = true;
    }
    
    if (!found) return defaultVal;
    return negative ? -result : result;
  }
  return defaultVal;
}
