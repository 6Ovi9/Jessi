#include "compass.h"
#include <InternalFileSystem.h>
#include <cfloat>

using namespace Adafruit_LittleFS_Namespace;

// Helper delay for I2C timing
static inline void delay_half() {
  delayMicroseconds(15);
}

// Helper macros for fast GPIO control
#define SCL_HIGH() (sclPort->DIRCLR = sclBit)
#define SCL_LOW()  (sclPort->DIRSET = sclBit)
#define SDA_HIGH() (sdaPort->DIRCLR = sdaBit)
#define SDA_LOW()  (sdaPort->DIRSET = sdaBit)
#define SDA_READ() ((sdaPort->IN & sdaBit) != 0)
#define SCL_READ() ((sclPort->IN & sclBit) != 0)

CompassController::CompassController()
    : sensor_connected(false), powered_down(false), sensor_address(0x1C),
      zero_reading_count(0), conversion_pending(false), trigger_ms(0),
      current_heading(0.0f), heading_filtered(0.0f), last_update_ms(0),
      calibrating(false), calibration_start_ms(0), min_x(0), max_x(0), min_y(0),
      max_y(0), min_z(0), max_z(0), offset_x(COMPASS_HARD_IRON_X),
      offset_y(COMPASS_HARD_IRON_Y), offset_z(COMPASS_HARD_IRON_Z),
      scale_x(COMPASS_SOFT_IRON_XX), scale_y(COMPASS_SOFT_IRON_YY),
      scale_z(COMPASS_SOFT_IRON_ZZ), raw_x(0.0f), raw_y(0.0f), raw_z(0.0f),
      history_idx(0), history_filled(false),
      sclPort(nullptr), sdaPort(nullptr), sclBit(0), sdaBit(0) {
  memset(history_x, 0, sizeof(history_x));
  memset(history_y, 0, sizeof(history_y));
}

// ============================================================================
// BIT-BANG I2C IMPLEMENTATION
// ============================================================================

void CompassController::i2c_init() {
  uint32_t pin_scl = g_ADigitalPinMap[PIN_COMPASS_SCL];
  sclPort = (pin_scl < 32) ? NRF_P0 : NRF_P1;
  sclBit = 1UL << (pin_scl & 31);

  uint32_t pin_sda = g_ADigitalPinMap[PIN_COMPASS_SDA];
  sdaPort = (pin_sda < 32) ? NRF_P0 : NRF_P1;
  sdaBit = 1UL << (pin_sda & 31);

  sclPort->OUTCLR = sclBit;
  sdaPort->OUTCLR = sdaBit;

  sclPort->PIN_CNF[pin_scl & 31] = (GPIO_PIN_CNF_DIR_Input << GPIO_PIN_CNF_DIR_Pos) |
                                   (GPIO_PIN_CNF_INPUT_Connect << GPIO_PIN_CNF_INPUT_Pos) |
                                   (GPIO_PIN_CNF_PULL_Pullup << GPIO_PIN_CNF_PULL_Pos);

  sdaPort->PIN_CNF[pin_sda & 31] = (GPIO_PIN_CNF_DIR_Input << GPIO_PIN_CNF_DIR_Pos) |
                                   (GPIO_PIN_CNF_INPUT_Connect << GPIO_PIN_CNF_INPUT_Pos) |
                                   (GPIO_PIN_CNF_PULL_Pullup << GPIO_PIN_CNF_PULL_Pos);
}

void CompassController::i2c_start() {
  SDA_HIGH(); SCL_HIGH(); delay_half();
  SDA_LOW(); delay_half();
  SCL_LOW(); delay_half();
}

void CompassController::i2c_stop() {
  SCL_LOW(); delay_half();
  SDA_LOW(); delay_half();
  SCL_HIGH(); delay_half();
  
  // Clock stretching support during STOP
  int timeout = 10;
  while (!SCL_READ() && timeout-- > 0) delay_half();
  
  SDA_HIGH(); delay_half();
}

bool CompassController::i2c_wb(uint8_t b) {
  for (int i = 7; i >= 0; i--) {
    if (b & (1 << i)) SDA_HIGH();
    else SDA_LOW();
    delay_half();
    SCL_HIGH(); delay_half();
    
    int timeout = 10;
    while (!SCL_READ() && timeout-- > 0) delay_half();
    
    SCL_LOW(); delay_half();
  }
  
  // Read ACK
  SDA_HIGH(); delay_half();
  SCL_HIGH(); delay_half();
  
  int timeout = 1000;
  while (!SCL_READ() && timeout-- > 0) delay_half();
  
  bool ack = !SDA_READ();
  SCL_LOW(); delay_half();
  return ack;
}

uint8_t CompassController::i2c_rb(bool ack) {
  uint8_t b = 0;
  SDA_HIGH();
  for (int i = 7; i >= 0; i--) {
    delay_half();
    SCL_HIGH(); delay_half();
    
    int timeout = 10;
    while (!SCL_READ() && timeout-- > 0) delay_half();
    
    if (SDA_READ()) b |= (1 << i);
    SCL_LOW(); delay_half();
  }
  
  if (ack) SDA_LOW();
  else SDA_HIGH();
  
  delay_half();
  SCL_HIGH(); delay_half();
  
  int timeout = 1000;
  while (!SCL_READ() && timeout-- > 0) delay_half();
  
  SCL_LOW(); delay_half();
  SDA_HIGH();
  return b;
}

bool CompassController::wreg(uint8_t reg, uint8_t val) {
  i2c_start();
  if (!i2c_wb(sensor_address << 1)) { i2c_stop(); return false; }
  if (!i2c_wb(reg)) { i2c_stop(); return false; }
  if (!i2c_wb(val)) { i2c_stop(); return false; }
  i2c_stop();
  return true;
}

bool CompassController::rreg(uint8_t reg, uint8_t &val) {
  i2c_start();
  if (!i2c_wb(sensor_address << 1)) { i2c_stop(); return false; }
  if (!i2c_wb(reg)) { i2c_stop(); return false; }
  i2c_start(); // Repeated start
  if (!i2c_wb((sensor_address << 1) | 1)) { i2c_stop(); return false; }
  val = i2c_rb(false); // NACK on last byte
  i2c_stop();
  return true;
}

bool CompassController::readAxes(float &cx, float &cy, float &cz) {
  i2c_start();
  if (!i2c_wb(sensor_address << 1)) { i2c_stop(); return false; }
  if (!i2c_wb(LIS3MDL_REG_OUT_X_L | 0x80)) { i2c_stop(); return false; } // bit7=1 auto-increment
  i2c_start();
  if (!i2c_wb((sensor_address << 1) | 1)) { i2c_stop(); return false; }
  
  uint8_t xl = i2c_rb(true);
  uint8_t xh = i2c_rb(true);
  uint8_t yl = i2c_rb(true);
  uint8_t yh = i2c_rb(true);
  uint8_t zl = i2c_rb(true);
  uint8_t zh = i2c_rb(false); // NACK on last
  i2c_stop();

  int16_t rx = (int16_t)(xl | ((uint16_t)xh << 8));
  int16_t ry = (int16_t)(yl | ((uint16_t)yh << 8));
  int16_t rz = (int16_t)(zl | ((uint16_t)zh << 8));

  const float scale = 100.0f / LIS3MDL_SENSITIVITY;
  cx = rx * scale;
  cy = ry * scale;
  cz = rz * scale;
  return true;
}

// ============================================================================
// COMPASS LOGIC
// ============================================================================

void CompassController::begin() {
  Serial.println("[COMPASS] Initializing Bit-Bang I2C (D4=SDA, D5=SCL)...");
  loadCalibration();

  i2c_init();

  // Recovery scanner loop (127-address) to clear any stuck SDA
  Serial.println("[COMPASS] Running recovery scanner loop...");
  for (int a = 1; a < 127; a++) {
    i2c_start();
    i2c_wb(a << 1);
    i2c_stop();
  }
  delay(10);

  bool who_ok = false;
  uint8_t who = 0;
  sensor_address = 0x1C;
  for (int i = 0; i < 5; i++) {
    if (rreg(LIS3MDL_REG_WHO_AM_I, who)) {
      if (who == LIS3MDL_WHO_AM_I_VALUE) {
        who_ok = true;
        break;
      }
    }
    delay(10);
  }

  if (!who_ok) {
    sensor_address = 0x1E;
    for (int i = 0; i < 5; i++) {
      if (rreg(LIS3MDL_REG_WHO_AM_I, who)) {
        if (who == LIS3MDL_WHO_AM_I_VALUE) {
          who_ok = true;
          break;
        }
      }
      delay(10);
    }
  }

  if (!who_ok) {
    Serial.println("[COMPASS] ✗ LIS3MDL not found on Bit-Bang I2C.");
    sensor_connected = false;
    return;
  }

  sensor_connected = true;
  Serial.print("[COMPASS] ✓ LIS3MDL found at 0x");
  Serial.println(sensor_address, HEX);

  // Write default config
  wreg(LIS3MDL_REG_CTRL1, 0x1C); // 80Hz Output Data Rate
  wreg(LIS3MDL_REG_CTRL2, 0x00); // 4G
  wreg(LIS3MDL_REG_CTRL3, 0x03); // Power-down initially
  wreg(LIS3MDL_REG_CTRL4, 0x00);
  wreg(LIS3MDL_REG_CTRL5, 0x40);

  conversion_pending = false;
  history_filled = false;
  history_idx = 0;
}

void CompassController::reinitBus() {
  i2c_init();
  for (int a = 1; a < 127; a++) { i2c_start(); i2c_wb(a << 1); i2c_stop(); }
  wreg(LIS3MDL_REG_CTRL1, 0x1C); 
  wreg(LIS3MDL_REG_CTRL2, 0x00); 
  wreg(LIS3MDL_REG_CTRL3, 0x03); 
  wreg(LIS3MDL_REG_CTRL4, 0x00);
  wreg(LIS3MDL_REG_CTRL5, 0x40);
}

void CompassController::powerDown() {
  if (!sensor_connected || powered_down) return;
  wreg(LIS3MDL_REG_CTRL3, 0x03);
  powered_down = true;
  conversion_pending = false;
  
  // Avoid floating lines by pulling them LOW explicitly
  uint32_t pin_scl = g_ADigitalPinMap[PIN_COMPASS_SCL];
  sclPort->OUTCLR = sclBit;
  sclPort->PIN_CNF[pin_scl & 31] = (GPIO_PIN_CNF_DIR_Output << GPIO_PIN_CNF_DIR_Pos) |
                                   (GPIO_PIN_CNF_INPUT_Disconnect << GPIO_PIN_CNF_INPUT_Pos);
  
  uint32_t pin_sda = g_ADigitalPinMap[PIN_COMPASS_SDA];
  sdaPort->OUTCLR = sdaBit;
  sdaPort->PIN_CNF[pin_sda & 31] = (GPIO_PIN_CNF_DIR_Output << GPIO_PIN_CNF_DIR_Pos) |
                                   (GPIO_PIN_CNF_INPUT_Disconnect << GPIO_PIN_CNF_INPUT_Pos);
  
  Serial.println("[COMPASS] Power-Down mode activated (pullups disabled)");
}

void CompassController::powerUp() {
  if (!sensor_connected || !powered_down) return;
  
  // Re-enable I2C pullups
  uint32_t pin_scl = g_ADigitalPinMap[PIN_COMPASS_SCL];
  sclPort->PIN_CNF[pin_scl & 31] = (GPIO_PIN_CNF_DIR_Input << GPIO_PIN_CNF_DIR_Pos) |
                                   (GPIO_PIN_CNF_INPUT_Connect << GPIO_PIN_CNF_INPUT_Pos) |
                                   (GPIO_PIN_CNF_PULL_Pullup << GPIO_PIN_CNF_PULL_Pos);
  
  uint32_t pin_sda = g_ADigitalPinMap[PIN_COMPASS_SDA];
  sdaPort->PIN_CNF[pin_sda & 31] = (GPIO_PIN_CNF_DIR_Input << GPIO_PIN_CNF_DIR_Pos) |
                                   (GPIO_PIN_CNF_INPUT_Connect << GPIO_PIN_CNF_INPUT_Pos) |
                                   (GPIO_PIN_CNF_PULL_Pullup << GPIO_PIN_CNF_PULL_Pos);
                                   
  powered_down = false;
  conversion_pending = false;
  i2c_start();
  i2c_stop();
  Serial.println("[COMPASS] Power restored");
}

void CompassController::update(float ax, float ay, float az) {
  if (powered_down || !sensor_connected) return;

  uint32_t now_ms = millis();

  // Step 1: Trigger measurement
  if (!conversion_pending) {
    if (now_ms - last_update_ms < (1000u / COMPASS_UPDATE_RATE_HZ)) return;

    if (wreg(LIS3MDL_REG_CTRL3, 0x01)) { // Single conversion mode
      conversion_pending = true;
      trigger_ms = now_ms;
    }
    return; // Return immediately to avoid blocking UI
  }

  // Timeout recovery for hung conversion
  if (now_ms - trigger_ms > 100) {
    conversion_pending = false;
    zero_reading_count++;
    if (zero_reading_count >= 3) {
      zero_reading_count = 0;
      reinitBus();
    }
    return;
  }

  // Step 2: Poll and Read
  uint8_t stat = 0;
  if (!rreg(LIS3MDL_REG_STATUS, stat) || !(stat & 0x08)) {
    return; // Not ready yet, try next tick
  }

  float rx, ry, rz;
  if (!readAxes(rx, ry, rz)) {
    conversion_pending = false;
    return;
  }

  conversion_pending = false; // Data successfully read
  last_update_ms = now_ms;

  if (!(rx == 0.0f && ry == 0.0f && rz == 0.0f)) {
    raw_x = -rx; // 180º Flip Fix
    raw_y = -ry; // 180º Flip Fix
    raw_z = rz;
    zero_reading_count = 0;
  } else {
    zero_reading_count++;
    if (zero_reading_count >= 3) {
      Serial.println("[COMPASS] 3 zero reads - reinitializing");
      zero_reading_count = 0;
      reinitBus();
      return;
    }
  }

  if (calibrating) calibrationUpdate();

  float cx = raw_x;
  float cy = raw_y;
  float cz = raw_z;

  _applyCalibration(cx, cy, cz);

  // Align LIS3MDL axes to the Watch Body (LSM6DS3) before tilt compensation.
  // Hardware empirical math: LSM6DS3 Y is Left, but LIS3MDL Y is Right.
  // Invert cy to make it Left-aligned so tilt compensation formulas work correctly.
  float cx_aligned = cx;
  float cy_aligned = -cy;
  float cz_aligned = cz;

  float cx_comp = cx_aligned;
  float cy_comp = cy_aligned;
  float accel_mag = sqrtf(ax * ax + ay * ay + az * az);
  if (accel_mag > 0.1f) {
    float norm_ax = ax / accel_mag;
    float norm_ay = ay / accel_mag;
    float norm_az = az / accel_mag;

    float clamped_ax = -norm_ax;
    if (clamped_ax > 1.0f) clamped_ax = 1.0f;
    if (clamped_ax < -1.0f) clamped_ax = -1.0f;
    float pitch = asin(clamped_ax);
    float roll = atan2(norm_ay, norm_az);

    float sinPitch = sin(pitch);
    float cosPitch = cos(pitch);
    float sinRoll = sin(roll);
    float cosRoll = cos(roll);

    cx_comp = cx_aligned * cosPitch + cy_aligned * sinRoll * sinPitch + cz_aligned * cosRoll * sinPitch;
    cy_comp = cy_aligned * cosRoll - cz_aligned * sinRoll;
  }

  history_x[history_idx] = cx_comp;
  history_y[history_idx] = cy_comp;
  history_idx++;
  if (history_idx >= FILTER_WINDOW) {
    history_idx = 0;
    if (!history_filled) {
      history_filled = true;
      heading_filtered = _calculateHeading(cx_comp, cy_comp);
    }
  }

  float filtered_cx = cx_comp;
  float filtered_cy = cy_comp;

  if (history_filled) {
    float sum_x = 0;
    float sum_y = 0;
    for (int i = 0; i < FILTER_WINDOW; i++) {
      sum_x += history_x[i];
      sum_y += history_y[i];
    }
    filtered_cx = sum_x / FILTER_WINDOW;
    filtered_cy = sum_y / FILTER_WINDOW;
  }

  float heading = _calculateHeading(filtered_cx, filtered_cy);

  // Initialize heading_filtered instantly on the first valid sample
  if (!history_filled && history_idx == 1) {
    heading_filtered = heading;
  }

  float diff = heading - heading_filtered;
  if (diff > 180.0f) diff -= 360.0f;
  if (diff < -180.0f) diff += 360.0f;
  heading_filtered += diff * COMPASS_HEADING_ALPHA;

  heading_filtered = fmod(heading_filtered, 360.0f);
  if (heading_filtered < 0) heading_filtered += 360.0f;

  current_heading = heading_filtered;
}

float CompassController::_calculateHeading(float x, float y) {
  // Convert raw X/Y (already hard-iron corrected and tilt compensated) to degrees.
  // x is Forward, y is Left.
  // To get a standard clockwise compass heading (0=N, 90=E), we use atan2(-y, x).
  float heading_deg = atan2(-y, x) * 180.0f / (float)M_PI;
  return fmod(heading_deg + 360.0f, 360.0f);
}

void CompassController::_applyCalibration(float &x, float &y, float &z) {
  x = (scale_x > 0.001f) ? (x - offset_x) / scale_x : 0;
  y = (scale_y > 0.001f) ? (y - offset_y) / scale_y : 0;
  z = (scale_z > 0.001f) ? (z - offset_z) / scale_z : 0;
}

void CompassController::startCalibration() {
  calibrating = true;
  calibration_start_ms = millis();
  min_x = FLT_MAX;  max_x = -FLT_MAX;
  min_y = FLT_MAX;  max_y = -FLT_MAX;
  min_z = FLT_MAX;  max_z = -FLT_MAX;
  Serial.println("[COMPASS] Calibration started — rotate the watch in all directions.");
}

void CompassController::calibrationUpdate() {
  if (!calibrating) return;

  if (raw_x == 0.0f && raw_y == 0.0f && raw_z == 0.0f) return;

  if (raw_x < min_x) min_x = raw_x;
  if (raw_x > max_x) max_x = raw_x;
  if (raw_y < min_y) min_y = raw_y;
  if (raw_y > max_y) max_y = raw_y;
  if (raw_z < min_z) min_z = raw_z;
  if (raw_z > max_z) max_z = raw_z;

  if ((millis() - calibration_start_ms) > 10000) {
    calibrating = false;
    if (min_x == FLT_MAX || (max_x - min_x) < 10.0f) {
      return;
    }

    offset_x = (max_x + min_x) / 2.0f;
    offset_y = (max_y + min_y) / 2.0f;
    offset_z = (max_z + min_z) / 2.0f;

    scale_x = (max_x - min_x) / 2.0f;
    scale_y = (max_y - min_y) / 2.0f;
    scale_z = (max_z - min_z) / 2.0f;

    // Enforce a minimum scale to avoid division by zero
    if (scale_x < 0.001f) scale_x = 1.0f;
    if (scale_y < 0.001f) scale_y = 1.0f;
    if (scale_z < 0.001f) scale_z = 1.0f;

    Serial.println("[COMPASS] Calibration done. Saving...");
    saveCalibration();
  }
}

void CompassController::saveCalibration() {
  struct __attribute__((packed)) CompassCalibData {
    float offset_x;
    float offset_y;
    float offset_z;
    float scale_x;
    float scale_y;
    float scale_z;
    uint16_t magic;
    uint8_t checksum;
  };

  CompassCalibData data;
  memset(&data, 0, sizeof(data)); // Ensures 1-byte padding at end is 0
  
  data.offset_x = offset_x;
  data.offset_y = offset_y;
  data.offset_z = offset_z;
  data.scale_x = scale_x;
  data.scale_y = scale_y;
  data.scale_z = scale_z;
  data.magic = 0xC0A6;

  uint8_t sum = 0;
  const uint8_t *ptr = (const uint8_t *)&data;
  for (size_t i = 0; i < offsetof(CompassCalibData, checksum); i++) sum += ptr[i];
  data.checksum = sum;

  const char* TMP_FILE = "compass.tmp";
  InternalFS.remove(TMP_FILE);
  
  File file(InternalFS);
  if (file.open(TMP_FILE, FILE_O_WRITE)) {
    if (file.write((const uint8_t *)&data, sizeof(data)) != sizeof(data)) {
      file.close();
      InternalFS.remove(TMP_FILE);
      Serial.println("[COMPASS] ✗ Failed to write calibration.");
      return;
    }
    file.close();
    
    InternalFS.remove("compass.dat");
    if (InternalFS.rename(TMP_FILE, "compass.dat")) {
      Serial.println("[COMPASS] Calibration saved to flash.");
    } else {
      Serial.println("[COMPASS] ✗ Failed to rename calibration temp file.");
    }
  } else {
    Serial.println("[COMPASS] ✗ Failed to open temp file.");
  }
}

void CompassController::loadCalibration() {
  struct __attribute__((packed)) CompassCalibData {
    float offset_x;
    float offset_y;
    float offset_z;
    float scale_x;
    float scale_y;
    float scale_z;
    uint16_t magic;
    uint8_t checksum;
  };

  File file(InternalFS);
  if (!file.open("compass.dat", FILE_O_READ)) {
    Serial.println("[COMPASS] No calibration file found (compass.dat).");
    return;
  }

  CompassCalibData data;
  memset(&data, 0, sizeof(data)); // Added for absolute safety
  
  if (file.read(&data, sizeof(data)) != sizeof(data)) {
    file.close();
    Serial.println("[COMPASS] ✗ Failed to read full calibration data.");
    return;
  }
  file.close();

  if (data.magic != 0xC0A6) {
    Serial.println("[COMPASS] ✗ Invalid calibration magic bytes (v2 format required).");
    return;
  }

  uint8_t sum = 0;
  const uint8_t *ptr = (const uint8_t *)&data;
  for (size_t i = 0; i < offsetof(CompassCalibData, checksum); i++) {
    sum += ptr[i];
  }

  if (data.checksum != sum) {
    Serial.println("[COMPASS] ✗ Calibration checksum failed! Data corrupt.");
    return;
  }

  offset_x = data.offset_x;
  offset_y = data.offset_y;
  offset_z = data.offset_z;
  scale_x = data.scale_x;
  scale_y = data.scale_y;
  scale_z = data.scale_z;

  Serial.println("[COMPASS] ✓ Calibration loaded successfully.");
}