#ifndef LED_CONTROLLER_H
#define LED_CONTROLLER_H

#include "config.h"
#include "runtime_config.h"
#include <Arduino.h>
#include <Adafruit_NeoPixel.h>
#include <cmath>

// ============================================================================
// LED RING CONTROLLER (SK6812 MINI-E - 12 RGB+W)
// ============================================================================

class LEDController {
public:
  LEDController();
  
  // Initialize NeoPixel library
  void begin();
  
  // Set runtime config pointer (for dynamic colors from app)
  void setRuntimeConfig(const RuntimeConfigManager* mgr) { runtime_cfg = mgr; }
  
  // Power control (D10 MOSFET)
  void setPower(bool on);
  bool isPowerOn() const { return power_on; }
  
  // Clear all LEDs and show
  void clear();
  void show();
  
  // Set individual LED color (ARGB8888)
  void setLED(uint8_t index, uint32_t color);
  void setLEDBrightness(uint8_t index, uint32_t color, uint8_t brightness);
  
  // Fill all LEDs with color
  void fillAll(uint32_t color);
  void fillWithBrightness(uint32_t color, uint8_t brightness);
  
  // Clock display (hour, minute, second hands)
  void showClock(bool connected);
  void updateClockTime(uint8_t hours, uint8_t minutes, uint8_t seconds, uint16_t millis = 0);
  
  // Radar mode (single LED pointing to partner)
  void showRadar(float bearing_relative);
  
  // Distance mode (fill from LED 1 to LED 11 based on distance)
  void showDistance(uint32_t distance_m);
  
  // Animation: flash pattern
  void flash(uint32_t color, uint8_t count, uint16_t duration_ms);
  void pulse(uint32_t color, uint16_t period_ms);
  
  // Error/status animations
  void errorNoGPS();  // 3 rapid red pulses
  void errorBattery(); // 1 slow red pulse
  void successOTA();   // Green flash
  
  // Haptic RX animation (pink pulse sync with vibration)
  void animateHapticRX();
  
  // OTA progress animation
  void updateOTAProgress(uint8_t percentage);
  
  // Update function (call from loop for animations)
  void update(uint32_t now_ms);
  
  // Brightness scaling with gamma correction
  static uint8_t gammaScale(uint8_t value, uint8_t max_brightness = 255);
  
  // Helper to extract color components
  static uint8_t getRed(uint32_t color)   { return (color >> 16) & 0xFF; }
  static uint8_t getGreen(uint32_t color) { return (color >> 8) & 0xFF; }
  static uint8_t getBlue(uint32_t color)  { return color & 0xFF; }
  static uint8_t getAlpha(uint32_t color) { return (color >> 24) & 0xFF; }

private:
  Adafruit_NeoPixel pixels;
  bool power_on;
  
  // Clock state
  uint8_t current_hour;
  uint8_t current_minute;
  uint8_t current_second;
  uint16_t current_millis;
  bool clock_connected;
  uint32_t clock_last_update_ms;
  
  // Animation state
  uint32_t animation_start_ms;
  uint32_t animation_last_ms;
  bool animation_active;
  GestureType animation_type;
  uint8_t animation_count;
  uint16_t animation_duration;
  
  // Radar state
  float radar_bearing;
  bool radar_active;
  
  // Distance state
  uint32_t distance_m;
  bool distance_active;
  
  // Runtime config (optional, for dynamic colors)
  const RuntimeConfigManager* runtime_cfg;
  
  // Helper methods
  int _mapBearingToLED(float bearing);
  int _mapDistanceToLEDCount(uint32_t distance_m);
  uint32_t _getDistanceColor(uint32_t distance_m);
  
  // Interpolation for smooth transitions
  void _setLEDSmooth(uint8_t led_index, uint32_t color, float brightness_fraction);
};

#endif // LED_CONTROLLER_H
