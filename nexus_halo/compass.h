#ifndef COMPASS_H
#define COMPASS_H

#include "config.h"
#include <Arduino.h>
#include <Wire.h>

// ============================================================================
// COMPASS CONTROLLER (LIS3MDL Magnetometer on custom I2C D4/D5)
// Sin dependencia de Adafruit_LIS3MDL — lectura directa por I2C.
//
// LIS3MDL registros clave:
//   0x0F  WHO_AM_I       → 0x3D si el chip es correcto
//   0x20  CTRL_REG1      → ODR, performance mode, TEMP_EN
//   0x21  CTRL_REG2      → rango (FS)
//   0x22  CTRL_REG3      → modo operación: 0x00 = continuous
//   0x23  CTRL_REG4      → eje Z performance
//   0x24  CTRL_REG5      → BDU (bit6)
//   0x27  STATUS_REG     → bit3 = ZYXDA (nuevo dato listo)
//   0x28  OUT_X_L        → inicio datos (6 bytes, little-endian, XYZ)
//
// Sensibilidad FS=±4 Gauss: 6842 LSB/Gauss
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
#define LIS3MDL_SENSITIVITY                                                    \
  6842.0f // LSB/Gauss at ±4G → divide para µT (*100/6842)

class CompassController {
public:
  CompassController();

  // Inicializa el magnetómetro en Wire (D4=SDA, D5=SCL)
  void begin();

  // Lee y actualiza el heading (llamar desde loop)
  void update();

  // Heading actual en grados (0–360, Norte = 0)
  float getHeading() const { return current_heading; }

  // Calibración manual (el usuario rota el reloj)
  void startCalibration();
  bool isCalibrating() const { return calibrating; }
  void calibrationUpdate();

  // Persistencia en flash
  void saveCalibration();
  void loadCalibration();

  // Diagnóstico
  bool isConnected() const { return sensor_connected; }
  float getRawX() const { return raw_x; }
  float getRawY() const { return raw_y; }
  float getRawZ() const { return raw_z; }

private:
  bool sensor_connected;
  uint8_t sensor_address;
  uint8_t zero_reading_count;

  float current_heading;
  float heading_filtered;
  uint32_t last_update_ms;

  // Calibración hard-iron / soft-iron
  bool calibrating;
  uint32_t calibration_start_ms;
  float min_x, max_x, min_y, max_y, min_z, max_z;
  float offset_x, offset_y, offset_z;
  float scale_x, scale_y, scale_z;

  // Lecturas raw (µT)
  float raw_x, raw_y, raw_z;

  // Filtro de mediana (ventana 5)
  static const int FILTER_WINDOW = 5;
  float history_x[FILTER_WINDOW];
  float history_y[FILTER_WINDOW];
  int history_idx;
  bool history_filled;

  // I2C helpers
  bool _writeReg(uint8_t reg, uint8_t value);
  bool _readReg(uint8_t reg, uint8_t &value);
  bool _readRaw(float &x, float &y, float &z);
  bool _softReset();
  bool _configure();
  bool _resetBus();

  // Cálculo
  float _calculateHeading(float x, float y);
  void _applyCalibration(float &x, float &y, float &z);
};

#endif // COMPASS_H