#ifndef COMPASS_H
#define COMPASS_H

#include "config.h"
#include <Arduino.h>
#include "variant.h"

// ============================================================================
// COMPASS CONTROLLER (LIS3MDL Magnetometer on custom I2C D4/D5)
// Bit-bang I2C implementation to bypass hardware TWIM lockups.
// ============================================================================

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

class CompassController {
public:
  CompassController();

  void begin();
  void reinitBus();
  void update(float ax = 0.0f, float ay = 0.0f, float az = 1.0f);
  float getHeading() const { return current_heading; }
  void startCalibration();
  bool isCalibrating() const { return calibrating; }
  void calibrationUpdate();
  void saveCalibration();
  void loadCalibration();
  void powerDown();
  void powerUp();
  bool isPoweredDown() const { return powered_down; }
  bool isConnected() const { return sensor_connected; }
  float getRawX() const { return raw_x; }
  float getRawY() const { return raw_y; }
  float getRawZ() const { return raw_z; }

private:
  bool sensor_connected;
  bool powered_down;
  uint8_t sensor_address;
  uint16_t zero_reading_count;

  // Single conversion sequence state
  bool conversion_pending;
  uint32_t trigger_ms;

  float current_heading;
  float heading_filtered;
  uint32_t last_update_ms;

  bool calibrating;
  uint32_t calibration_start_ms;
  float min_x, max_x, min_y, max_y, min_z, max_z;
  float offset_x, offset_y, offset_z;
  float scale_x, scale_y, scale_z;

  float raw_x, raw_y, raw_z;

  static const int FILTER_WINDOW = 2;
  float history_x[FILTER_WINDOW];
  float history_y[FILTER_WINDOW];
  int history_idx;
  bool history_filled;

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
  void _applyCalibration(float &x, float &y, float &z);
};

#endif // COMPASS_H