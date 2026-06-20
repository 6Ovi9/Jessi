#ifndef COMPASS_H
#define COMPASS_H

#include "config.h"
#include <Arduino.h>
#include <Adafruit_LIS3MDL.h>

// ============================================================================
// COMPASS CONTROLLER (LIS3MDL Magnetometer on custom I2C D4/D5)
// ============================================================================

class CompassController {
public:
  CompassController();
  
  // Initialize magnetometer on custom I2C pins
  void begin();
  
  // Read and update heading (call from loop)
  void update();
  
  // Get current heading (0-360 degrees, North = 0)
  float getHeading() const { return current_heading; }
  
  // Start calibration routine (user manual rotation required)
  void startCalibration();
  bool isCalibrating() const { return calibrating; }
  void calibrationUpdate();
  
  // Save/load calibration from flash
  void saveCalibration();
  void loadCalibration();
  
  // Magnetometer diagnostics
  bool isConnected() const { return sensor_connected; }
  float getRawX() const { return raw_x; }
  float getRawY() const { return raw_y; }
  float getRawZ() const { return raw_z; }

private:
  Adafruit_LIS3MDL sensor;
  bool sensor_connected;
  
  // Current heading
  float current_heading;
  float heading_filtered;
  uint32_t last_update_ms;
  
  // Calibration
  bool calibrating;
  uint32_t calibration_start_ms;
  float min_x, max_x, min_y, max_y, min_z, max_z;
  
  // Hard-iron and soft-iron offsets (calibration parameters)
  float offset_x, offset_y, offset_z;
  float scale_x, scale_y, scale_z;
  
  // Raw readings
  float raw_x, raw_y, raw_z;
  
  // Helper methods
  float _calculateHeading(float x, float y);
  void _applyCalibration(float& x, float& y, float& z);
};

#endif // COMPASS_H
