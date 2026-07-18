#include "test_compass.h"
#include <math.h>

static inline void delay_half() {
  delayMicroseconds(15);
}

#define SCL_HIGH() (sclPort->DIRCLR = sclBit)
#define SCL_LOW()  (sclPort->DIRSET = sclBit)
#define SDA_HIGH() (sdaPort->DIRCLR = sdaBit)
#define SDA_LOW()  (sdaPort->DIRSET = sdaBit)
#define SDA_READ() ((sdaPort->IN & sdaBit) != 0)
#define SCL_READ() ((sclPort->IN & sclBit) != 0)

TestCompass::TestCompass()
    : sensor_connected(false), sensor_address(0x1C),
      current_heading(0.0f), raw_x(0.0f), raw_y(0.0f), raw_z(0.0f),
      offset_x(0.0f), offset_y(0.0f), offset_z(0.0f),
      sclPort(nullptr), sdaPort(nullptr), sclBit(0), sdaBit(0) {
}

void TestCompass::i2c_init() {
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

void TestCompass::i2c_start() {
  SDA_HIGH(); SCL_HIGH(); delay_half();
  SDA_LOW(); delay_half();
  SCL_LOW(); delay_half();
}

void TestCompass::i2c_stop() {
  SCL_LOW(); delay_half();
  SDA_LOW(); delay_half();
  SCL_HIGH(); delay_half();
  int timeout = 10;
  while (!SCL_READ() && timeout-- > 0) delay_half();
  SDA_HIGH(); delay_half();
}

bool TestCompass::i2c_wb(uint8_t b) {
  for (int i = 7; i >= 0; i--) {
    if (b & (1 << i)) SDA_HIGH();
    else SDA_LOW();
    delay_half();
    SCL_HIGH(); delay_half();
    int timeout = 10;
    while (!SCL_READ() && timeout-- > 0) delay_half();
    SCL_LOW(); delay_half();
  }
  SDA_HIGH(); delay_half();
  SCL_HIGH(); delay_half();
  int timeout = 1000;
  while (!SCL_READ() && timeout-- > 0) delay_half();
  bool ack = !SDA_READ();
  SCL_LOW(); delay_half();
  return ack;
}

uint8_t TestCompass::i2c_rb(bool ack) {
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

bool TestCompass::wreg(uint8_t reg, uint8_t val) {
  i2c_start();
  if (!i2c_wb(sensor_address << 1)) { i2c_stop(); return false; }
  if (!i2c_wb(reg)) { i2c_stop(); return false; }
  if (!i2c_wb(val)) { i2c_stop(); return false; }
  i2c_stop();
  return true;
}

bool TestCompass::rreg(uint8_t reg, uint8_t &val) {
  i2c_start();
  if (!i2c_wb(sensor_address << 1)) { i2c_stop(); return false; }
  if (!i2c_wb(reg)) { i2c_stop(); return false; }
  i2c_start(); 
  if (!i2c_wb((sensor_address << 1) | 1)) { i2c_stop(); return false; }
  val = i2c_rb(false);
  i2c_stop();
  return true;
}

bool TestCompass::readAxes(float &cx, float &cy, float &cz) {
  i2c_start();
  if (!i2c_wb(sensor_address << 1)) { i2c_stop(); return false; }
  if (!i2c_wb(LIS3MDL_REG_OUT_X_L | 0x80)) { i2c_stop(); return false; } 
  i2c_start();
  if (!i2c_wb((sensor_address << 1) | 1)) { i2c_stop(); return false; }
  
  uint8_t xl = i2c_rb(true);
  uint8_t xh = i2c_rb(true);
  uint8_t yl = i2c_rb(true);
  uint8_t yh = i2c_rb(true);
  uint8_t zl = i2c_rb(true);
  uint8_t zh = i2c_rb(false); 
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

void TestCompass::begin() {
  i2c_init();

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
    Serial.println("[TEST COMPASS] LIS3MDL not found!");
    sensor_connected = false;
    return;
  }

  sensor_connected = true;
  Serial.print("[TEST COMPASS] LIS3MDL found at 0x");
  Serial.println(sensor_address, HEX);

  wreg(LIS3MDL_REG_CTRL1, 0x1C); // 80Hz Output Data Rate
  wreg(LIS3MDL_REG_CTRL2, 0x00); // 4G
  wreg(LIS3MDL_REG_CTRL3, 0x00); // Continuous mode
  wreg(LIS3MDL_REG_CTRL4, 0x00);
  wreg(LIS3MDL_REG_CTRL5, 0x40);
}

float TestCompass::_calculateHeading(float x, float y) {
  if (x == 0 && y == 0) return 0;
  // Calculate standard heading
  float heading = atan2(y, x) * 180.0f / M_PI;
  
  // Apply the 180 degree clockwise rotation requested by user
  heading += 180.0f;
  
  if (heading >= 360.0f) heading -= 360.0f;
  if (heading < 0) heading += 360.0f;
  return heading;
}

bool TestCompass::update() {
  if (!sensor_connected) return false;

  uint8_t status = 0;
  if (!rreg(LIS3MDL_REG_STATUS, status)) return false;
  if ((status & 0x08) == 0) return false; 

  float cx, cy, cz;
  if (readAxes(cx, cy, cz)) {
    raw_x = cx;
    raw_y = cy;
    raw_z = cz;

    // Apply offset (Hard Iron)
    float cal_x = raw_x - offset_x;
    float cal_y = raw_y - offset_y;

    current_heading = _calculateHeading(cal_x, cal_y);
    return true;
  }
  return false;
}
