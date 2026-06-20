#include "led_controller.h"

LEDController::LEDController()
  : pixels(LED_COUNT, PIN_LED_RING_DATA, NEO_GRBW + NEO_KHZ800),
    power_on(false),
    current_hour(0),
    current_minute(0),
    current_second(0),
    current_millis(0),
    clock_connected(true),
    clock_last_update_ms(0),
    animation_active(false),
    animation_type(GESTURE_NONE),
    animation_count(0),
    animation_duration(0),
    radar_bearing(0),
    radar_active(false),
    distance_m(0),
    distance_active(false),
    runtime_cfg(nullptr)
{
}

void LEDController::begin() {
  pinMode(PIN_LED_POWER, OUTPUT);
  pixels.begin();
  pixels.setBrightness(255);  // We'll do manual brightness scaling
  clear();
}

void LEDController::setPower(bool on) {
  power_on = on;
  digitalWrite(PIN_LED_POWER, on ? HIGH : LOW);
  if (!on) {
    clear();  // Clear display when powering off
  }
}

void LEDController::clear() {
  for (int i = 0; i < LED_COUNT; i++) {
    pixels.setPixelColor(i, 0, 0, 0, 0);  // GRBW format
  }
  show();
}

void LEDController::show() {
  pixels.show();
}

void LEDController::setLED(uint8_t index, uint32_t color) {
  if (index >= LED_COUNT || !power_on) return;
  
  uint8_t r = getRed(color);
  uint8_t g = getGreen(color);
  uint8_t b = getBlue(color);
  uint8_t w = 0;  // White channel (if used)
  
  // For SK6812, GRB order is typical in Adafruit_NeoPixel
  pixels.setPixelColor(index, g, r, b, w);
}

void LEDController::setLEDBrightness(uint8_t index, uint32_t color, uint8_t brightness) {
  if (index >= LED_COUNT || !power_on) return;
  
  uint8_t r = (getRed(color) * brightness) / 255;
  uint8_t g = (getGreen(color) * brightness) / 255;
  uint8_t b = (getBlue(color) * brightness) / 255;
  
  pixels.setPixelColor(index, g, r, b, 0);
}

void LEDController::fillAll(uint32_t color) {
  if (!power_on) return;
  
  uint8_t r = getRed(color);
  uint8_t g = getGreen(color);
  uint8_t b = getBlue(color);
  
  for (int i = 0; i < LED_COUNT; i++) {
    pixels.setPixelColor(i, g, r, b, 0);
  }
  show();
}

void LEDController::fillWithBrightness(uint32_t color, uint8_t brightness) {
  if (!power_on) return;
  
  uint8_t r = (getRed(color) * brightness) / 255;
  uint8_t g = (getGreen(color) * brightness) / 255;
  uint8_t b = (getBlue(color) * brightness) / 255;
  
  for (int i = 0; i < LED_COUNT; i++) {
    pixels.setPixelColor(i, g, r, b, 0);
  }
  show();
}

void LEDController::updateClockTime(uint8_t hours, uint8_t minutes, uint8_t seconds, uint16_t millis_val) {
  current_hour = hours % 12;  // 12-hour format
  current_minute = minutes;
  current_second = seconds;
  current_millis = millis_val;
  clock_last_update_ms = millis();
}

void LEDController::showClock(bool connected) {
  if (!power_on) return;
  
  clock_connected = connected;
  clear();
  
  // Calculate LED positions for each hand
  int hour_led = current_hour;  // 0-11 maps directly to LED 0-11
  
  int minute_led = (current_minute * 12) / 60;  // Map 0-59 to 0-11
  
  // Seconds with interpolation
  float second_pos = (current_second + current_millis / 1000.0f) * 12.0f / 60.0f;
  int second_led = (int)floor(second_pos);
  float second_fraction = second_pos - floor(second_pos);
  int second_led_next = (second_led + 1) % LED_COUNT;
  
  // Get colors based on connection state — use runtime config if available
  uint32_t color_hour, color_minute, color_second;
  uint8_t brightness_pct;
  
  if (runtime_cfg) {
    const RuntimeConfig& cfg = runtime_cfg->getConfig();
    color_hour   = connected ? cfg.colorHoursConnected   : cfg.colorHoursDisc;
    color_minute = connected ? cfg.colorMinutesConnected : cfg.colorMinutesDisc;
    color_second = connected ? cfg.colorSecondsConnected : cfg.colorSecondsDisc;
    brightness_pct = cfg.brightnessPercent;
  } else {
    color_hour   = connected ? COLOR_HOURS_CONNECTED   : COLOR_HOURS_DISC;
    color_minute = connected ? COLOR_MINUTES_CONNECTED : COLOR_MINUTES_DISC;
    color_second = connected ? COLOR_SECONDS_CONNECTED : COLOR_SECONDS_DISC;
    brightness_pct = LED_CLOCK_BRIGHTNESS;
  }
  
  // Apply brightness scaling
  uint8_t base_brightness = (brightness_pct * 255) / 100;
  
  // Set hour (discrete)
  setLEDBrightness(hour_led, color_hour, base_brightness);
  
  // Set minute (discrete)
  setLEDBrightness(minute_led, color_minute, base_brightness);
  
  // Set seconds (interpolated between two LEDs)
  uint8_t brightness_current = gammaScale((1.0f - second_fraction) * base_brightness);
  uint8_t brightness_next = gammaScale(second_fraction * base_brightness);
  
  setLEDBrightness(second_led, color_second, brightness_current);
  setLEDBrightness(second_led_next, color_second, brightness_next);
  
  show();
}

void LEDController::showRadar(float bearing_relative) {
  if (!power_on) return;
  
  radar_bearing = bearing_relative;
  radar_active = true;
  
  clear();
  
  // Map bearing (0-360°) to LED index (0-11)
  int led_index = _mapBearingToLED(bearing_relative);
  setLEDBrightness(led_index, COLOR_RADAR, LED_CLOCK_BRIGHTNESS * 255 / 100);
  
  // Optionally add interpolation for smooth motion
  // (more complex, defer for now)
  
  show();
}

void LEDController::showDistance(uint32_t dist) {
  if (!power_on) return;
  
  this->distance_m = dist;
  distance_active = true;
  
  clear();
  
  // Convert distance to km
  float distance_km = dist / 1000.0f;
  if (distance_km > DISTANCE_THRESHOLD_MAX_KM) distance_km = DISTANCE_THRESHOLD_MAX_KM;
  
  // Calculate total LEDs to fill (1-11) based on distance
  float normalized = distance_km / (float)DISTANCE_THRESHOLD_MAX_KM;
  int total_leds = (int)(normalized * 11) + 1;
  if (total_leds > 11) total_leds = 11;
  if (total_leds < 1) total_leds = 1;
  
  uint8_t base_brightness = (LED_CLOCK_BRIGHTNESS * 255) / 100;
  
  // Fill LEDs from 0 to total_leds, each LED gets color based on its
  // position in the distance scale (per spec v1.2):
  //   LEDs 0-3  (0-15km)    → Blue
  //   LEDs 4-6  (15-50km)   → Green
  //   LEDs 7-8  (50-150km)  → Yellow
  //   LEDs 9-10 (150-350km) → Orange
  //   LED  11   (350-500km) → Red
  for (int i = 0; i < total_leds && i < 12; i++) {
    uint32_t color;
    if (i <= 3)       color = COLOR_DISTANCE_NEAR;      // Blue
    else if (i <= 6)  color = COLOR_DISTANCE_PROVINCE;  // Green
    else if (i <= 8)  color = COLOR_DISTANCE_FAR;        // Yellow
    else if (i <= 10) color = COLOR_DISTANCE_VFAR;       // Orange
    else              color = COLOR_DISTANCE_EXTREME;    // Red
    
    setLEDBrightness(i, color, base_brightness);
  }
  
  show();
}

void LEDController::update(uint32_t now_ms) {
  // Handle animation updates if active
  if (animation_active) {
    uint32_t elapsed = now_ms - animation_start_ms;
    
    // This is where we'd update animation frames
    // For now, simple placeholder
  }
}

void LEDController::errorNoGPS() {
  if (!power_on) return;
  
  // 3 rapid red pulses
  fillAll(COLOR_ERROR);
  show();
}

void LEDController::successOTA() {
  if (!power_on) return;
  
  // Green flash
  fillAll(COLOR_SUCCESS);
  show();
}

void LEDController::animateHapticRX() {
  if (!power_on) return;
  
  // Pink pulse all LEDs
  fillWithBrightness(COLOR_HAPTIC_RX, 128);
}

void LEDController::updateOTAProgress(uint8_t percentage) {
  if (!power_on) return;
  
  clear();
  
  // Calculate how many LEDs to fill (1-12)
  int leds_to_fill = (percentage * 12) / 100;
  if (leds_to_fill > 12) leds_to_fill = 12;
  
  uint32_t color = (percentage < 100) ? COLOR_INFO : COLOR_SUCCESS;
  uint8_t brightness = 200;
  
  for (int i = 0; i < leds_to_fill; i++) {
    setLEDBrightness(i, color, brightness);
  }
  
  show();
}

uint8_t LEDController::gammaScale(uint8_t value, uint8_t max_brightness) {
  // Apply gamma correction for perceived linearity
  float normalized = (float)value / 255.0f;
  float corrected = pow(normalized, 1.0f / DUMMY_BRIGHTNESS_LOGARITHMIC);
  return (uint8_t)(corrected * max_brightness);
}

int LEDController::_mapBearingToLED(float bearing) {
  // bearing: 0-360 degrees (0 = north, increases clockwise)
  // LED 0 is at 12h (north), LED 3 at 3h (east), etc.
  
  int led_index = (int)round((bearing / 360.0f) * LED_COUNT) % LED_COUNT;
  return led_index;
}

int LEDController::_mapDistanceToLEDCount(uint32_t distance_m) {
  // Convert meters to distance in km
  float distance_km = distance_m / 1000.0f;
  
  if (distance_km < 0) distance_km = 0;
  if (distance_km > DISTANCE_THRESHOLD_MAX_KM) distance_km = DISTANCE_THRESHOLD_MAX_KM;
  
  // Map linearly to 1-11 LEDs (LED 0 reserved or different)
  int led_count = (int)((distance_km / DISTANCE_THRESHOLD_MAX_KM) * 11) + 1;
  if (led_count > 11) led_count = 11;
  if (led_count < 1) led_count = 1;
  
  return led_count;
}

uint32_t LEDController::_getDistanceColor(uint32_t distance_m) {
  float distance_km = distance_m / 1000.0f;
  
  if (distance_km < DISTANCE_THRESHOLD_1_KM)      return COLOR_DISTANCE_NEAR;
  if (distance_km < DISTANCE_THRESHOLD_2_KM)      return COLOR_DISTANCE_PROVINCE;
  if (distance_km < DISTANCE_THRESHOLD_3_KM)      return COLOR_DISTANCE_FAR;
  if (distance_km < DISTANCE_THRESHOLD_4_KM)      return COLOR_DISTANCE_VFAR;
  return COLOR_DISTANCE_EXTREME;
}
