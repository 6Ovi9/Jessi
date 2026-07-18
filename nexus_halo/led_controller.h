#ifndef LED_CONTROLLER_H
#define LED_CONTROLLER_H

#include "config.h"
#include "runtime_config.h"
#include <Arduino.h>
#include <cmath>

class LEDController {
public:
  LEDController();
  
  void begin();
  void setRuntimeConfig(const RuntimeConfigManager* mgr) { runtime_cfg = mgr; }
  void setPower(bool on);
  bool isPowerOn() const { return power_on; }
  
  void clear();
  void show();
  
  void setLED(uint8_t index, uint32_t color);
  void setLEDBrightness(uint8_t index, uint32_t color, uint8_t brightness);
  void addLEDBrightness(uint8_t index, uint32_t color, uint8_t brightness);
  
  void fillAll(uint32_t color);
  void fillWithBrightness(uint32_t color, uint8_t brightness);
  
  void showClock(bool connected);
  void updateClockTime(uint8_t hours, uint8_t minutes, uint8_t seconds, uint16_t millis = 0);
  
  void showRadar(float bearing_relative);
  void showDistance(uint32_t distance_m);
  
  void errorNoGPS();
  void errorBattery();
  void successOTA();
  void animateHapticRX();
  void updateOTAProgress(uint8_t percentage);
  void update(uint32_t now_ms);
  
  static uint8_t getRed(uint32_t color)   { return (color >> 16) & 0xFF; }
  static uint8_t getGreen(uint32_t color) { return (color >> 8) & 0xFF; }
  static uint8_t getBlue(uint32_t color)  { return color & 0xFF; }
  static uint8_t getAlpha(uint32_t color) { return (color >> 24) & 0xFF; }

  uint8_t _brightnessFromPct(uint8_t pct) const;

private:
  // Buffer for raw PWM Sequence (24 bits per LED + 200 padding words for 250us reset gap)
  uint16_t pwm_buffer[LED_COUNT * 32 + 200];
  
  // Local GRB frame buffers
  uint8_t r_buf[LED_COUNT];
  uint8_t g_buf[LED_COUNT];
  uint8_t b_buf[LED_COUNT];
  uint8_t w_buf[LED_COUNT];

  bool power_on;
  uint8_t current_hour;
  uint8_t current_minute;
  uint8_t current_second;
  uint16_t current_millis;
  bool clock_connected;
  uint32_t clock_last_update_ms;
  
  float radar_bearing;
  bool radar_active;
  uint32_t distance_m;
  bool distance_active;
  
  const RuntimeConfigManager* runtime_cfg;
  
  void _updatePWMBuffer();
};

#endif // LED_CONTROLLER_H
