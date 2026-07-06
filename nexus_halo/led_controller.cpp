#include "led_controller.h"

#define PWM_TOP_VALUE   20
#define PWM_T0H          5   // 5 ticks × 62.5ns = 312.5ns  (SK6812MINI-E-012 T0H spec: 250–400ns)
#define PWM_T1H         15   // 15 ticks × 62.5ns = 937.5ns (SK6812MINI-E-012 T1H spec: 700–1000ns)
#define PWM_RESET_WORDS 200  // 200 × 1.25µs = 250µs reset gap (spec: >80µs)

// POLARITY: bit 15 of every PWM compare value selects "start HIGH, go LOW at count".
// Without it, nRF52 PWM defaults to the opposite polarity (start LOW, go HIGH),
// which inverts every data bit and drives the reset gap HIGH instead of LOW —
// causing all LEDs to show wrong/no color. Adafruit's nRF52 NeoPixel driver ORs
// this bit for exactly the same reason.
#define PWM_POLARITY_BIT 0x8000

LEDController::LEDController()
  : power_on(false),
    current_hour(0), current_minute(0), current_second(0), current_millis(0),
    clock_connected(true), clock_last_update_ms(0),
    radar_bearing(0), radar_active(false), distance_m(0), distance_active(false),
    runtime_cfg(nullptr)
{
  memset(pwm_buffer, 0, sizeof(pwm_buffer));
  memset(r_buf, 0, sizeof(r_buf));
  memset(g_buf, 0, sizeof(g_buf));
  memset(b_buf, 0, sizeof(b_buf));
  memset(w_buf, 0, sizeof(w_buf));
}

void LEDController::begin() {
  pinMode(PIN_LED_POWER, OUTPUT);
  digitalWrite(PIN_LED_POWER, LOW);

  // Ensure data pin starts LOW before PWM takes over
  pinMode(PIN_LED_RING_DATA, OUTPUT);
  digitalWrite(PIN_LED_RING_DATA, LOW);

  uint32_t pin_num = g_ADigitalPinMap[PIN_LED_RING_DATA];

  // Use NRF_PWM1 — NRF_PWM0 may be claimed by the Adafruit BSP for analogWrite()
  NRF_PWM1->ENABLE = 0; // Disable during config

  NRF_PWM1->PSEL.OUT[0] = pin_num;        // Connect data pin
  NRF_PWM1->PSEL.OUT[1] = 0x80000000UL;  // Disconnect (bit 31 = 1)
  NRF_PWM1->PSEL.OUT[2] = 0x80000000UL;  // Disconnect
  NRF_PWM1->PSEL.OUT[3] = 0x80000000UL;  // Disconnect

  NRF_PWM1->MODE       = 0; // Up counting
  NRF_PWM1->PRESCALER  = 0; // 16 MHz base clock (62.5 ns/tick)
  NRF_PWM1->COUNTERTOP = PWM_TOP_VALUE; // 20 ticks → 1.25 µs period (800 kHz)
  NRF_PWM1->LOOP       = 0; // No loop

  // DECODER.LOAD values (nRF52840 PS §6.24):
  //   0 = Common     — ONE halfword per period drives all 4 channels identically.
  //                    Each halfword in pwm_buffer is one PWM period for the data pin.
  //   1 = Grouped    — TWO halfwords per period.
  //   2 = Individual — FOUR halfwords per period.
  //   3 = Waveform   — FOUR halfwords per period; 4th overrides COUNTERTOP.
  //                    *** LOAD=3 corrupts every frame; SEQEND never fires. ***
  //                    *** Previously had (3UL<<0) here by mistake.         ***
  NRF_PWM1->DECODER = (0UL << 0); // LOAD=Common(0) | MODE=RefCountTop(0)

  // Auto-stop after each sequence so repeated SEQSTART calls don't race.
  NRF_PWM1->SHORTS = PWM_SHORTS_SEQEND0_STOP_Msk;

  NRF_PWM1->ENABLE = 1;

  clear();
}


void LEDController::setPower(bool on) {
  if (power_on == on) return;
  if (on) {
    power_on = true;
    digitalWrite(PIN_LED_POWER, HIGH);
    delay(10);
    clear();
    show(); // Push a clean frame immediately on wake
  } else {
    clear();
    show(); // Will now execute successfully via EasyDMA
    delayMicroseconds(300); // Wait for DMA + reset gap
    power_on = false;
    digitalWrite(PIN_LED_POWER, LOW);
  }
}

// =============================================================================
// Brightness helper
// Converts a 0-100 brightness percentage to the 0-255 byte value sent to the
// LEDs. When logarithmicBrightness is enabled in the runtime config (set via
// the app), applies gamma-2.2 correction so the brightness scale FEELS linear
// to the eye. Without gamma correction, the upper half of the scale looks much
// brighter than the lower half because human perception is nonlinear.
// =============================================================================
uint8_t LEDController::_brightnessFromPct(uint8_t pct) const {
  if (pct > 100) pct = 100;
  if (pct == 0) return 0;
  bool use_gamma = runtime_cfg && runtime_cfg->getConfig().logarithmicBrightness;
  if (use_gamma) {
    float normalized    = pct / 100.0f;
    float gamma_applied = powf(normalized, DUMMY_BRIGHTNESS_LOGARITHMIC);
    uint8_t b = (uint8_t)(gamma_applied * 255.0f + 0.5f);
    return (b == 0) ? 1 : b;  // any nonzero % should show something
  }
  return (uint8_t)((pct * 255u) / 100u);
}

void LEDController::_updatePWMBuffer() {
  int idx = 0;
  for (int i = 0; i < LED_COUNT; i++) {
    // GRBW channel order: SK6812MINI-E expects Green, Red, Blue, White on the wire
    uint8_t c[4] = { g_buf[i], r_buf[i], b_buf[i], w_buf[i] };
    for (int ch = 0; ch < 4; ch++) {
      for (int bit = 7; bit >= 0; bit--) {
        // PWM_POLARITY_BIT (0x8000) selects "start HIGH, go LOW at compare count".
        // Without it the nRF52 PWM peripheral uses the opposite polarity and the
        // entire SK6812 waveform is inverted — the root cause of LEDs never lighting.
        pwm_buffer[idx++] = ((c[ch] & (1 << bit)) ? PWM_T1H : PWM_T0H) | PWM_POLARITY_BIT;
      }
    }
  }
  // Reset gap: compare=0 with polarity bit → pin stays LOW for 250µs
  while (idx < (LED_COUNT * 32 + PWM_RESET_WORDS)) {
    pwm_buffer[idx++] = PWM_POLARITY_BIT; // 0 compare | polarity = LOW
  }
}

void LEDController::show() {
  if (!power_on) return;
  _updatePWMBuffer();

  // Re-enable PWM — the SEQEND0→STOP shortcut stops it after each frame.
  // Must be done before writing SEQ registers.
  NRF_PWM1->ENABLE = 1;

  NRF_PWM1->SEQ[0].PTR      = (uint32_t)(pwm_buffer);
  NRF_PWM1->SEQ[0].CNT      = (LED_COUNT * 32 + PWM_RESET_WORDS); // count of 16-bit halfwords
  NRF_PWM1->SEQ[0].REFRESH  = 0;
  NRF_PWM1->SEQ[0].ENDDELAY = 0;

  NRF_PWM1->EVENTS_SEQEND[0] = 0;
  NRF_PWM1->EVENTS_STOPPED   = 0;
  NRF_PWM1->TASKS_SEQSTART[0] = 1;

  // Block until SEQEND fires (DMA done, last bit clocked out).
  // Actual frame time for 12 LEDs: 288 bits × 1.25µs + 250µs reset ≈ 0.61ms.
  // Safety timeout = 20ms.
  uint32_t deadline = millis() + 20;
  while (!NRF_PWM1->EVENTS_SEQEND[0] && (int32_t)(millis() - deadline) < 0) {}

  if (!NRF_PWM1->EVENTS_SEQEND[0]) {
    // DMA never completed — peripheral mis-configured or buffer issue.
    // Log if Serial is available; don't hang.
    return;
  }

  // Wait for peripheral to actually stop (SHORTS wired SEQEND0→STOP).
  // Ensures pin is LOW/idle before the next frame starts.
  deadline = millis() + 5;
  while (!NRF_PWM1->EVENTS_STOPPED && (int32_t)(millis() - deadline) < 0) {}
}

void LEDController::clear() {
  memset(r_buf, 0, sizeof(r_buf));
  memset(g_buf, 0, sizeof(g_buf));
  memset(b_buf, 0, sizeof(b_buf));
  memset(w_buf, 0, sizeof(w_buf));
}

void LEDController::setLED(uint8_t index, uint32_t color) {
  if (index >= LED_COUNT || !power_on) return;
  uint8_t physical_index = index % LED_COUNT; 
  r_buf[physical_index] = getRed(color);
  g_buf[physical_index] = getGreen(color);
  b_buf[physical_index] = getBlue(color);
  w_buf[physical_index] = getAlpha(color);
}

void LEDController::setLEDBrightness(uint8_t index, uint32_t color, uint8_t brightness) {
  if (index >= LED_COUNT || !power_on) return;
  uint8_t physical_index = index % LED_COUNT; 
  r_buf[physical_index] = (getRed(color) * brightness) / 255;
  g_buf[physical_index] = (getGreen(color) * brightness) / 255;
  b_buf[physical_index] = (getBlue(color) * brightness) / 255;
  w_buf[physical_index] = (getAlpha(color) * brightness) / 255;
}

void LEDController::addLEDBrightness(uint8_t index, uint32_t color, uint8_t brightness) {
  if (index >= LED_COUNT || !power_on) return;
  uint8_t physical_index = index % LED_COUNT; 
  uint16_t nr = r_buf[physical_index] + (getRed(color) * brightness) / 255;
  uint16_t ng = g_buf[physical_index] + (getGreen(color) * brightness) / 255;
  uint16_t nb = b_buf[physical_index] + (getBlue(color) * brightness) / 255;
  uint16_t nw = w_buf[physical_index] + (getAlpha(color) * brightness) / 255;
  r_buf[physical_index] = nr > 255 ? 255 : nr;
  g_buf[physical_index] = ng > 255 ? 255 : ng;
  b_buf[physical_index] = nb > 255 ? 255 : nb;
  w_buf[physical_index] = nw > 255 ? 255 : nw;
}

void LEDController::fillAll(uint32_t color) {
  if (!power_on) return;
  for (int i = 0; i < LED_COUNT; i++) {
    r_buf[i] = getRed(color);
    g_buf[i] = getGreen(color);
    b_buf[i] = getBlue(color);
    w_buf[i] = getAlpha(color);
  }
  show();
}

void LEDController::fillWithBrightness(uint32_t color, uint8_t brightness) {
  if (!power_on) return;
  for (int i = 0; i < LED_COUNT; i++) {
    r_buf[i] = (getRed(color) * brightness) / 255;
    g_buf[i] = (getGreen(color) * brightness) / 255;
    b_buf[i] = (getBlue(color) * brightness) / 255;
    w_buf[i] = (getAlpha(color) * brightness) / 255;
  }
  show();
}

void LEDController::updateClockTime(uint8_t hours, uint8_t minutes, uint8_t seconds, uint16_t millis_val) {
  current_hour = hours % 12;
  current_minute = minutes;
  current_second = seconds;
  current_millis = millis_val;
  clock_last_update_ms = millis();
}

void LEDController::showClock(bool connected) {
  if (!power_on) return;
  clock_connected = connected;
  clear();

  // ---- Hours: one LED per hour (LED 0 = 12:xx, LED 6 = 6:xx) ----
  int hour_led = current_hour; // 0-11 maps directly to LED 0-11

  // ---- Minutes: two adjacent LEDs at equal brightness ----
  // 12 LEDs cover 60 min → each LED = 5 min.
  // Lighting the current 5-min block AND the next gives a rough
  // "you're between X:05 and X:10" reading without sub-LED interpolation.
  int minute_led      = (current_minute * 12) / 60;
  int minute_led_next = (minute_led + 1) % LED_COUNT;

  // ---- Seconds: snap to the LED for the current 5-second tick ----
  // 12 LEDs × 5 s = 60 s. No crossfade — at normal brightness the
  // fractional interpolation was visually distracting and added no
  // useful info at this scale. The hand simply steps every 5 seconds.
  uint32_t elapsed_ms = millis() - clock_last_update_ms + current_millis;
  uint32_t total_s    = (uint32_t)current_second + elapsed_ms / 1000;
  int second_led      = (int)((total_s / 5) % LED_COUNT);

  uint32_t color_hour, color_minute, color_second;
  uint8_t brightness_pct = runtime_cfg ? runtime_cfg->getConfig().brightnessPercent : LED_CLOCK_BRIGHTNESS;
  uint8_t base_brightness = _brightnessFromPct(brightness_pct);

  if (runtime_cfg) {
    const RuntimeConfig& cfg = runtime_cfg->getConfig();
    color_hour   = connected ? cfg.colorHoursConnected   : cfg.colorHoursDisc;
    color_minute = connected ? cfg.colorMinutesConnected : cfg.colorMinutesDisc;
    color_second = connected ? cfg.colorSecondsConnected : cfg.colorSecondsDisc;
  } else {
    color_hour   = connected ? COLOR_HOURS_CONNECTED   : COLOR_HOURS_DISC;
    color_minute = connected ? COLOR_MINUTES_CONNECTED : COLOR_MINUTES_DISC;
    color_second = connected ? COLOR_SECONDS_CONNECTED : COLOR_SECONDS_DISC;
  }

  addLEDBrightness(hour_led,       color_hour,   base_brightness);
  addLEDBrightness(minute_led,     color_minute, base_brightness);
  addLEDBrightness(minute_led_next,color_minute, base_brightness);
  addLEDBrightness(second_led,     color_second, base_brightness);

  show();
}

void LEDController::showRadar(float bearing_relative) {
  if (!power_on) return;
  radar_bearing = bearing_relative;
  radar_active = true;
  clear();
  
  bearing_relative = fmod(bearing_relative, 360.0f);
  if (bearing_relative < 0) bearing_relative += 360.0f;

  float led_pos = (bearing_relative / 360.0f) * LED_COUNT;
  if (led_pos >= LED_COUNT) led_pos = 0;
  
  int led_index = (int)floor(led_pos);
  float fraction = led_pos - floor(led_pos);
  int led_next = (led_index + 1) % LED_COUNT;
  
  uint8_t brightness_pct = runtime_cfg ? runtime_cfg->getConfig().brightnessPercent : LED_CLOCK_BRIGHTNESS;
  uint8_t base_brightness = _brightnessFromPct(brightness_pct);
  uint8_t brightness_current = (uint8_t)(sqrtf(1.0f - fraction) * base_brightness);
  uint8_t brightness_next = (uint8_t)(sqrtf(fraction) * base_brightness);
  
  setLEDBrightness(led_index, COLOR_RADAR, brightness_current);
  setLEDBrightness(led_next, COLOR_RADAR, brightness_next);
  show();
}

void LEDController::showDistance(uint32_t dist) {
  if (!power_on) return;
  this->distance_m = dist;
  distance_active = true;
  clear();
  
  float d_km = dist / 1000.0f;
  if (d_km > DISTANCE_THRESHOLD_MAX_KM) d_km = DISTANCE_THRESHOLD_MAX_KM;
  if (d_km < 1.0f) d_km = 1.0f;
  
  float log_dist = log10(d_km);
  float log_max = log10((float)DISTANCE_THRESHOLD_MAX_KM);
  float normalized = log_dist / log_max; 
  if (normalized > 1.0f) normalized = 1.0f;
  if (normalized < 0.0f) normalized = 0.0f;
  
  float leds_float = normalized * LED_COUNT; 
  if (dist > 0 && leds_float < 0.1f) {
    leds_float = 0.1f; // Ensure at least faint glow for near distances
  }
  int total_leds = (int)leds_float;
  float partial_brightness = leds_float - (float)total_leds;
  
  if (total_leds >= LED_COUNT) {
    total_leds = LED_COUNT;
    partial_brightness = 0.0f;
  }
  
  uint8_t brightness_pct = runtime_cfg ? runtime_cfg->getConfig().brightnessPercent : LED_CLOCK_BRIGHTNESS;
  uint8_t base_brightness = _brightnessFromPct(brightness_pct);

  for (int i = 0; i < LED_COUNT; i++) {
    if (i > total_leds) break;
    uint32_t color;
    float segment_km = pow(10, ((float)i / (float)LED_COUNT) * log_max);
    if (segment_km <= DISTANCE_THRESHOLD_1_KM) color = COLOR_DISTANCE_NEAR;
    else if (segment_km <= DISTANCE_THRESHOLD_2_KM) color = COLOR_DISTANCE_PROVINCE;
    else if (segment_km <= DISTANCE_THRESHOLD_3_KM) color = COLOR_DISTANCE_FAR;
    else if (segment_km <= DISTANCE_THRESHOLD_4_KM) color = COLOR_DISTANCE_VFAR;
    else color = COLOR_DISTANCE_EXTREME;

    if (i < total_leds) {
      setLEDBrightness(i, color, base_brightness);
    } else if (i == total_leds && partial_brightness > 0.05f) {
      setLEDBrightness(i, color, (uint8_t)(base_brightness * partial_brightness));
    }
  }
  show();
}

void LEDController::update(uint32_t now_ms) {}

void LEDController::errorNoGPS() {
  if (!power_on) return;
  uint8_t brightness_pct = runtime_cfg ? runtime_cfg->getConfig().brightnessPercent : LED_CLOCK_BRIGHTNESS;
  uint8_t b = _brightnessFromPct(brightness_pct);

  clear();
  setLEDBrightness(0, COLOR_ERROR, b);
  setLEDBrightness(4, COLOR_ERROR, b);
  setLEDBrightness(8, COLOR_ERROR, b);
  show();
}

void LEDController::errorBattery() {
  if (!power_on) return;
  uint8_t brightness_pct = runtime_cfg ? runtime_cfg->getConfig().brightnessPercent : LED_CLOCK_BRIGHTNESS;
  uint8_t b = _brightnessFromPct(brightness_pct);

  clear();
  setLEDBrightness(6, COLOR_ERROR, b);
  show();
}

void LEDController::successOTA() {
  if (!power_on) return;
  uint8_t brightness_pct = runtime_cfg ? runtime_cfg->getConfig().brightnessPercent : LED_CLOCK_BRIGHTNESS;
  uint8_t b = _brightnessFromPct(brightness_pct);
  fillWithBrightness(COLOR_SUCCESS, b);
}

void LEDController::animateHapticRX() {
  if (!power_on) return;
  fillWithBrightness(COLOR_HAPTIC_RX, 128);
}

void LEDController::updateOTAProgress(uint8_t percentage) {
  if (!power_on) return;
  clear();
  int leds_to_fill = (percentage * LED_COUNT) / 100;
  if (leds_to_fill > LED_COUNT) leds_to_fill = LED_COUNT;
  uint32_t color = (percentage < 100) ? COLOR_INFO : COLOR_SUCCESS;
  for (int i = 0; i < leds_to_fill; i++) setLEDBrightness(i, color, 200);
  show();
}
