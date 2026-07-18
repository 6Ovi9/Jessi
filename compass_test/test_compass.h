#ifndef TEST_COMPASS_H
#define TEST_COMPASS_H

#include <Arduino.h>
#include "variant.h"

// Custom I2C pins for LIS3MDL on Nexus Halo
#define PIN_COMPASS_SDA D4
#define PIN_COMPASS_SCL D5

// LIS3MDL register map
#define LIS3MDL_REG_WHO_AM_I 0x0F
#define LIS3MDL_REG_CTRL1 0x20
#define LIS3MDL_REG_CTRL2 0x21
#define LIS3MDL_REG_CTRL3 0x22
#define LIS3MDL_REG_CTRL4 0x23
#define LIS3MDL_REG_CTRL5 0x24
#define LIS3MDL_REG_STATUS 0x27
#define LIS3MDL_REG_OUT_X_L 0x28

#define LIS3MDL_WHO_AM_I_VALUE 0x3D
#define LIS3MDL_SENSITIVITY 6842.0f // LSB/Gauss at ±4G

class TestCompass {
public:
  TestCompass();

  void begin();
  bool update();
  
  float getHeading() const { return current_heading; }
  float getRawX() const { return raw_x; }
  float getRawY() const { return raw_y; }
  float getRawZ() const { return raw_z; }

  // Calibration
  void setOffset(float ox, float oy, float oz) {
    offset_x = ox; offset_y = oy; offset_z = oz;
  }

private:
  bool sensor_connected;
  uint8_t sensor_address;

  float current_heading;
  float raw_x, raw_y, raw_z;
  
  float offset_x, offset_y, offset_z;

  // Bit-bang I2C variables
  NRF_GPIO_Type* sclPort;
  NRF_GPIO_Type* sdaPort;
  uint32_t sclBit;
  uint32_t sdaBit;

  // Bit-bang helper methods
  void i2c_init();
  void i2c_start();
  void i2c_stop();
  bool i2c_wb(uint8_t b);
  uint8_t i2c_rb(bool ack);
  bool wreg(uint8_t reg, uint8_t val);
  bool rreg(uint8_t reg, uint8_t &val);
  bool readAxes(float &cx, float &cy, float &cz);

  float _calculateHeading(float x, float y);
};

#endif // TEST_COMPASS_H
