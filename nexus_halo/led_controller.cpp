#include "led_controller.h"
extern "C" {
  #include <nrf_soc.h>
  #include <nrf_sdm.h>
  #include <nrf_nvic.h>
}

LEDController::LEDController()
  : pixels(LED_COUNT, PIN_LED_RING_DATA, NEO_GRB + NEO_KHZ800),
    power_on(false),
    ble_active(false),
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
  show();
}

void LEDController::setPower(bool on) {
  power_on = on;
  if (on) {
    digitalWrite(PIN_LED_POWER, HIGH);
    delay(10); // Esperar a que Vcc se estabilice antes de transmitir
  } else {
    // Transmitir todo a cero ANTES de cortar corriente
    for (int i = 0; i < LED_COUNT; i++) {
      pixels.setPixelColor(i, 0, 0, 0);
    }
    pixels.show();
    delayMicroseconds(50);
    digitalWrite(PIN_LED_POWER, LOW);
  }
}

void LEDController::clear() {
  for (int i = 0; i < LED_COUNT; i++) {
    pixels.setPixelColor(i, 0, 0, 0);
  }
}

void LEDController::show() {
  // Let Adafruit_NeoPixel handle its own interrupts and SoftDevice retries
  pixels.show();
}

void LEDController::setLED(uint8_t index, uint32_t color) {
  if (index >= LED_COUNT || !power_on) return;
  
  uint8_t physical_index = (12 - index) % 12;
  uint8_t r = getRed(color);
  uint8_t g = getGreen(color);
  uint8_t b = getBlue(color);
  
  bool use_gamma = true;
  if (runtime_cfg) {
    use_gamma = runtime_cfg->getConfig().logarithmicBrightness;
  }
  
  if (use_gamma) {
    r = Adafruit_NeoPixel::gamma8(r);
    g = Adafruit_NeoPixel::gamma8(g);
    b = Adafruit_NeoPixel::gamma8(b);
  }
  pixels.setPixelColor(physical_index, r, g, b);
}

void LEDController::setLEDBrightness(uint8_t index, uint32_t color, uint8_t brightness) {
  if (index >= LED_COUNT || !power_on) return;
  
  uint8_t physical_index = (12 - index) % 12;
  bool use_gamma = true;
  if (runtime_cfg) {
    use_gamma = runtime_cfg->getConfig().logarithmicBrightness;
  }
  
  uint8_t r, g, b;
  if (use_gamma) {
    uint8_t corrected_brightness = Adafruit_NeoPixel::gamma8(brightness);
    if (brightness > 0 && corrected_brightness < 8) {
      corrected_brightness = 8;
    }
    
    r = (Adafruit_NeoPixel::gamma8(getRed(color)) * corrected_brightness) / 255;
    g = (Adafruit_NeoPixel::gamma8(getGreen(color)) * corrected_brightness) / 255;
    b = (Adafruit_NeoPixel::gamma8(getBlue(color)) * corrected_brightness) / 255;
    
    // Protection: keep color components at least at 1 if they were non-zero and corrected_brightness > 0
    if (corrected_brightness > 0) {
      if (getRed(color) > 0 && r == 0) r = 1;
      if (getGreen(color) > 0 && g == 0) g = 1;
      if (getBlue(color) > 0 && b == 0) b = 1;
    }
  } else {
    r = (getRed(color) * brightness) / 255;
    g = (getGreen(color) * brightness) / 255;
    b = (getBlue(color) * brightness) / 255;
  }
  pixels.setPixelColor(physical_index, r, g, b);
}

void LEDController::fillAll(uint32_t color) {
  if (!power_on) return;
  
  uint8_t r = getRed(color);
  uint8_t g = getGreen(color);
  uint8_t b = getBlue(color);
  
  bool use_gamma = true;
  if (runtime_cfg) {
    use_gamma = runtime_cfg->getConfig().logarithmicBrightness;
  }
  
  if (use_gamma) {
    r = Adafruit_NeoPixel::gamma8(r);
    g = Adafruit_NeoPixel::gamma8(g);
    b = Adafruit_NeoPixel::gamma8(b);
  }
  
  for (int i = 0; i < LED_COUNT; i++) {
    pixels.setPixelColor(i, r, g, b);
  }
  show();
}

void LEDController::fillWithBrightness(uint32_t color, uint8_t brightness) {
  if (!power_on) return;
  
  bool use_gamma = true;
  if (runtime_cfg) {
    use_gamma = runtime_cfg->getConfig().logarithmicBrightness;
  }
  
  uint8_t r, g, b;
  if (use_gamma) {
    uint8_t corrected_brightness = Adafruit_NeoPixel::gamma8(brightness);
    if (brightness > 0 && corrected_brightness < 8) {
      corrected_brightness = 8;
    }
    
    r = (Adafruit_NeoPixel::gamma8(getRed(color)) * corrected_brightness) / 255;
    g = (Adafruit_NeoPixel::gamma8(getGreen(color)) * corrected_brightness) / 255;
    b = (Adafruit_NeoPixel::gamma8(getBlue(color)) * corrected_brightness) / 255;
    
    // Protection: keep color components at least at 1 if they were non-zero and corrected_brightness > 0
    if (corrected_brightness > 0) {
      if (getRed(color) > 0 && r == 0) r = 1;
      if (getGreen(color) > 0 && g == 0) g = 1;
      if (getBlue(color) > 0 && b == 0) b = 1;
    }
  } else {
    r = (getRed(color) * brightness) / 255;
    g = (getGreen(color) * brightness) / 255;
    b = (getBlue(color) * brightness) / 255;
  }
  
  for (int i = 0; i < LED_COUNT; i++) {
    pixels.setPixelColor(i, r, g, b);
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
  uint8_t brightness_current = (uint8_t)((1.0f - second_fraction) * base_brightness);
  uint8_t brightness_next = (uint8_t)(second_fraction * base_brightness);
  
  setLEDBrightness(second_led, color_second, brightness_current);
  setLEDBrightness(second_led_next, color_second, brightness_next);
  
  show();
}

void LEDController::showRadar(float bearing_relative) {
  if (!power_on) return;
  
  radar_bearing = bearing_relative;
  radar_active = true;
  
  clear();
  
  // Calculate continuous position (0 to 11.999...)
  float led_pos = (bearing_relative / 360.0f) * LED_COUNT;
  if (led_pos >= LED_COUNT) led_pos = 0;
  
  int led_index = (int)floor(led_pos);
  float fraction = led_pos - floor(led_pos);
  int led_next = (led_index + 1) % LED_COUNT;
  
  uint8_t brightness_pct;
  if (runtime_cfg) {
    brightness_pct = runtime_cfg->getConfig().brightnessPercent;
  } else {
    brightness_pct = LED_CLOCK_BRIGHTNESS;
  }
  uint8_t base_brightness = (brightness_pct * 255) / 100;
  
  uint8_t brightness_current = (uint8_t)((1.0f - fraction) * base_brightness);
  uint8_t brightness_next = (uint8_t)(fraction * base_brightness);
  
  setLEDBrightness(led_index, COLOR_RADAR, brightness_current);
  setLEDBrightness(led_next, COLOR_RADAR, brightness_next);
  
  show();
}

void LEDController::showDistance(uint32_t dist) {
  if (!power_on) return;
  
  this->distance_m = dist;
  distance_active = true;
  
  clear();
  
  // Calculamos la distancia en escala logarítmica para ser sensibles a cortas distancias.
  // 10 metros -> 1 LED
  // 100 metros -> ~3 LEDs
  // 1 km -> ~5 LEDs
  // 10 km -> ~7 LEDs
  // 100 km -> ~9 LEDs
  // 1000 km -> ~11 LEDs
  // 5000 km -> 12 LEDs
  float d_m = (float)dist;
  if (d_m < 10.0f) d_m = 10.0f; // Mínimo 10 metros para log10
  
  float log_dist = log10(d_m);
  float normalized = (log_dist - 1.0f) / 5.7f; // Rango de 1.0 (10m) a 6.7 (5000km)
  if (normalized > 1.0f) normalized = 1.0f;
  if (normalized < 0.0f) normalized = 0.0f;
  
  float leds_float = 1.0f + normalized * 11.0f; // 1.0 a 12.0
  int total_leds = (int)leds_float;
  float partial_brightness = leds_float - (float)total_leds;
  
  if (total_leds >= 12) {
    total_leds = 12;
    partial_brightness = 0.0f;
  }
  
  uint8_t brightness_pct;
  if (runtime_cfg) {
    brightness_pct = runtime_cfg->getConfig().brightnessPercent;
  } else {
    brightness_pct = LED_CLOCK_BRIGHTNESS;
  }
  uint8_t base_brightness = (brightness_pct * 255) / 100;
  
  // Fill LEDs from 0 to total_leds, each LED gets color based on its position
  for (int i = 0; i < 12; i++) {
    if (i > total_leds) break; // Optimization
    
    uint32_t color;
    if (i <= 3)       color = COLOR_DISTANCE_NEAR;      // Blue
    else if (i <= 6)  color = COLOR_DISTANCE_PROVINCE;  // Green
    else if (i <= 8)  color = COLOR_DISTANCE_FAR;        // Yellow
    else if (i <= 10) color = COLOR_DISTANCE_VFAR;       // Orange
    else              color = COLOR_DISTANCE_EXTREME;    // Red
    
    if (i < total_leds) {
      setLEDBrightness(i, color, base_brightness);
    } else if (i == total_leds && partial_brightness > 0.05f) {
      uint8_t pb = (uint8_t)(base_brightness * partial_brightness);
      setLEDBrightness(i, color, pb);
    }
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
  
  uint8_t brightness_pct = runtime_cfg ? runtime_cfg->getConfig().brightnessPercent : LED_CLOCK_BRIGHTNESS;
  uint8_t base_brightness = (brightness_pct * 255) / 100;
  
  fillWithBrightness(COLOR_ERROR, base_brightness);
}

void LEDController::successOTA() {
  if (!power_on) return;
  
  uint8_t brightness_pct = runtime_cfg ? runtime_cfg->getConfig().brightnessPercent : LED_CLOCK_BRIGHTNESS;
  uint8_t base_brightness = (brightness_pct * 255) / 100;
  
  fillWithBrightness(COLOR_SUCCESS, base_brightness);
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
