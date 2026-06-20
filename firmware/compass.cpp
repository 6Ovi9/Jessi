#include "compass.h"
#include <Wire.h>
#include <cmath>
#include <InternalFileSystem.h>

CompassController::CompassController()
  : sensor_connected(false),
    current_heading(0),
    heading_filtered(0),
    last_update_ms(0),
    calibrating(false),
    calibration_start_ms(0),
    min_x(0), max_x(0), min_y(0), max_y(0), min_z(0), max_z(0),
    offset_x(COMPASS_HARD_IRON_X),
    offset_y(COMPASS_HARD_IRON_Y),
    offset_z(COMPASS_HARD_IRON_Z),
    scale_x(COMPASS_SOFT_IRON_XX),
    scale_y(COMPASS_SOFT_IRON_YY),
    scale_z(COMPASS_SOFT_IRON_ZZ),
    raw_x(0), raw_y(0), raw_z(0)
{
}

void CompassController::begin() {
  // Set custom I2C pins for LIS3MDL
  Wire.setPins(PIN_COMPASS_SDA, PIN_COMPASS_SCL);
  Wire.begin();
  
  // Try to initialize LIS3MDL
  if (sensor.begin_I2C(COMPASS_I2C_ADDRESS)) {
    sensor_connected = true;
    
    // Configure for continuous measurement
    sensor.setDataRate(LIS3MDL_DATARATE_10_HZ);  // 10 Hz update
    sensor.setRange(LIS3MDL_RANGE_12_GAUSS);      // 12 Gauss range
    sensor.setIntThreshold(0);
    
    // Load calibration from flash (if available)
    loadCalibration();
  } else {
    sensor_connected = false;
    // Continue without compass; relying on app-provided bearing
  }
}

void CompassController::update() {
  if (!sensor_connected) return;
  
  uint32_t now_ms = millis();
  
  // Don't update too fast
  if ((now_ms - last_update_ms) < (1000 / COMPASS_UPDATE_RATE_HZ)) {
    return;
  }
  last_update_ms = now_ms;
  
  // Read magnetometer
  sensors_event_t event;
  sensor.getEvent(&event);
  
  raw_x = event.magnetic.x;
  raw_y = event.magnetic.y;
  raw_z = event.magnetic.z;
  
  // Apply calibration
  _applyCalibration(raw_x, raw_y, raw_z);
  
  // Calculate heading from X/Y (assume compass lies flat)
  float heading = _calculateHeading(raw_x, raw_y);
  
  // Apply low-pass filter for smoothing
  heading_filtered = heading_filtered * (1.0f - COMPASS_HEADING_ALPHA) +
                     heading * COMPASS_HEADING_ALPHA;
  
  current_heading = heading_filtered;
  
  // Normalize to 0-360
  if (current_heading < 0) current_heading += 360;
  if (current_heading >= 360) current_heading -= 360;
}

float CompassController::_calculateHeading(float x, float y) {
  // atan2(y, x) returns -π to π, convert to 0-360°
  float heading_rad = atan2(y, x);
  float heading_deg = heading_rad * 180.0f / M_PI;
  
  // Adjust for magnetic declination if needed (currently 0)
  // float declination = 0; // degrees
  // heading_deg += declination;
  
  // Normalize
  heading_deg = fmod(heading_deg + 360, 360);
  
  return heading_deg;
}

void CompassController::_applyCalibration(float& x, float& y, float& z) {
  // Remove hard-iron offset
  x -= offset_x;
  y -= offset_y;
  z -= offset_z;
  
  // Apply soft-iron scaling
  x *= scale_x;
  y *= scale_y;
  z *= scale_z;
}

void CompassController::startCalibration() {
  calibrating = true;
  calibration_start_ms = millis();
  min_x = max_x = raw_x;
  min_y = max_y = raw_y;
  min_z = max_z = raw_z;
}

void CompassController::calibrationUpdate() {
  if (!calibrating) return;
  
  // Track min/max values while rotating device
  if (raw_x < min_x) min_x = raw_x;
  if (raw_x > max_x) max_x = raw_x;
  if (raw_y < min_y) min_y = raw_y;
  if (raw_y > max_y) max_y = raw_y;
  if (raw_z < min_z) min_z = raw_z;
  if (raw_z > max_z) max_z = raw_z;
  
  // End calibration after 10 seconds
  if ((millis() - calibration_start_ms) > 10000) {
    calibrating = false;
    
    // Calculate offsets and scales
    offset_x = (max_x + min_x) / 2.0f;
    offset_y = (max_y + min_y) / 2.0f;
    offset_z = (max_z + min_z) / 2.0f;
    
    float range_x = (max_x - min_x) / 2.0f;
    float range_y = (max_y - min_y) / 2.0f;
    float range_z = (max_z - min_z) / 2.0f;
    
    float max_range = max(range_x, max(range_y, range_z));
    
    scale_x = (max_range > 0) ? max_range / range_x : 1.0f;
    scale_y = (max_range > 0) ? max_range / range_y : 1.0f;
    scale_z = (max_range > 0) ? max_range / range_z : 1.0f;
    
    // Save to flash
    saveCalibration();
  }
}

void CompassController::saveCalibration() {
  // Persist compass calibration to nRF52840 internal flash
  // Structure: magic(2) + offsets(12) + scales(12) + checksum(1) = 27 bytes
  
  struct CompassCalibData {
    uint16_t magic;
    float offset_x, offset_y, offset_z;
    float scale_x, scale_y, scale_z;
    uint8_t checksum;
  };
  
  CompassCalibData data;
  data.magic = 0xC0A5;  // "COAS" — compass calibration magic
  data.offset_x = offset_x;
  data.offset_y = offset_y;
  data.offset_z = offset_z;
  data.scale_x = scale_x;
  data.scale_y = scale_y;
  data.scale_z = scale_z;
  
  // Simple checksum (sum all bytes except checksum field)
  data.checksum = 0;
  const uint8_t* ptr = (const uint8_t*)&data;
  uint8_t sum = 0;
  for (size_t i = 0; i < sizeof(data) - 1; i++) {
    sum += ptr[i];
  }
  data.checksum = sum;
  
  File file(InternalFS);
  if (file.open("compass.dat", FILE_O_WRITE)) {
    file.write(&data, sizeof(data));
    file.close();
  }
}

void CompassController::loadCalibration() {
  struct CompassCalibData {
    uint16_t magic;
    float offset_x, offset_y, offset_z;
    float scale_x, scale_y, scale_z;
    uint8_t checksum;
  };
  
  File file(InternalFS);
  if (!file.open("compass.dat", FILE_O_READ)) {
    // No calibration file — use defaults from config.h
    return;
  }
  
  CompassCalibData data;
  if (file.read(&data, sizeof(data)) != sizeof(data)) {
    file.close();
    return;
  }
  file.close();
  
  // Verify magic
  if (data.magic != 0xC0A5) {
    return;
  }
  
  // Verify checksum
  const uint8_t* ptr = (const uint8_t*)&data;
  uint8_t sum = 0;
  for (size_t i = 0; i < sizeof(data) - 1; i++) {
    sum += ptr[i];
  }
  if (sum != data.checksum) {
    return;
  }
  
  // Apply loaded calibration
  offset_x = data.offset_x;
  offset_y = data.offset_y;
  offset_z = data.offset_z;
  scale_x = data.scale_x;
  scale_y = data.scale_y;
  scale_z = data.scale_z;
}
