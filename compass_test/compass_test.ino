#include <Arduino.h>
#include <Adafruit_NeoPixel.h>
#include "test_compass.h"

// Hardware pins
#define PIN_LED_RING_DATA D7
#define PIN_LED_POWER     D10

#define LED_COUNT 12

Adafruit_NeoPixel strip(LED_COUNT, PIN_LED_RING_DATA, NEO_GRB + NEO_KHZ800);
TestCompass compass;

// State
bool calibrating = false;
uint32_t cal_start_time = 0;
float min_x = 9999, max_x = -9999;
float min_y = 9999, max_y = -9999;

float offset_x = 0;
float offset_y = 0;

bool leds_enabled = true;

void setup() {
  Serial.begin(115200);
  
  // Power up LEDs
  pinMode(PIN_LED_POWER, OUTPUT);
  digitalWrite(PIN_LED_POWER, HIGH);
  delay(100);

  // Init LEDs
  strip.begin();
  strip.setBrightness(2); // Reduced from 30 to 2 to minimize magnetic interference
  strip.clear();
  strip.show();

  // Init compass
  compass.begin();

  Serial.println("Send 'C' via Serial Monitor to start calibration.");
  Serial.println("Send 'L' via Serial Monitor to toggle LEDs on/off (to test for interference).");
}

void loop() {
  bool start_cal = false;
  if (Serial.available() > 0) {
    char c = Serial.read();
    if (c == 'C' || c == 'c') start_cal = true;
    if (c == 'L' || c == 'l') {
      leds_enabled = !leds_enabled;
      if (!leds_enabled) {
        strip.clear();
        strip.show();
        digitalWrite(PIN_LED_POWER, LOW); // Physically cut power via MOSFET
        Serial.println("LEDs and MOSFET are now OFF (0mA current)");
      } else {
        digitalWrite(PIN_LED_POWER, HIGH); // Restore power
        delay(50); // Wait for LEDs to boot
        strip.begin();
        strip.setBrightness(2);
        Serial.println("LEDs and MOSFET are now ON");
      }
    }
  }

  if (start_cal && !calibrating) {
    // Start calibration
    calibrating = true;
    cal_start_time = millis();
    min_x = 9999; max_x = -9999;
    min_y = 9999; max_y = -9999;
    Serial.println("--- CALIBRATION STARTED ---");
    Serial.println("Spin the watch in a figure-8 or circles for 10 seconds!");
  }

  if (compass.update()) {
    float rx = compass.getRawX();
    float ry = compass.getRawY();
    float heading = compass.getHeading();

    if (calibrating) {
      // Record min/max
      if (rx < min_x) min_x = rx;
      if (rx > max_x) max_x = rx;
      if (ry < min_y) min_y = ry;
      if (ry > max_y) max_y = ry;

      // Show blue spinning animation
      strip.clear();
      strip.setPixelColor((millis() / 100) % LED_COUNT, strip.Color(0, 0, 255));
      strip.show();

      if (millis() - cal_start_time > 10000) {
        calibrating = false;
        offset_x = (max_x + min_x) / 2.0f;
        offset_y = (max_y + min_y) / 2.0f;
        compass.setOffset(offset_x, offset_y, 0);

        Serial.println("--- CALIBRATION FINISHED ---");
        Serial.print("OffsetX: "); Serial.println(offset_x);
        Serial.print("OffsetY: "); Serial.println(offset_y);
        Serial.println("Use these values in your config.h!");

        // Flash green
        strip.fill(strip.Color(0, 255, 0));
        strip.show();
        delay(1000);
      }
    } else {
      // Normal operation: Serial plotter format
      // Print X, Y and Heading
      Serial.print("RawX:"); Serial.print(rx); Serial.print(",");
      Serial.print("RawY:"); Serial.print(ry); Serial.print(",");
      Serial.print("Heading:"); Serial.println(heading);

      // Visualize North on the LED ring
      // 0 degrees is North. 
      // Assuming LED 0 is 12 o'clock.
      // We want the LED that points North to light up.
      // If heading is 90 (East), North is at 270 (Left), so LED 9.
      float north_angle = 360.0f - heading;
      if (north_angle >= 360.0f) north_angle -= 360.0f;

      // We have 12 LEDs, so each LED represents 30 degrees.
      // E.g., LED 0 = 0 deg, LED 1 = 30 deg, etc.
      float led_position = north_angle / 30.0f;
      
      strip.clear();
      
      // Calculate brightness for the 3 closest LEDs (anti-aliasing)
      for (int i = -1; i <= 1; i++) {
        // Find the adjacent LED index, wrapping around 0-11
        int led_idx = (round(led_position) + i + LED_COUNT) % LED_COUNT;
        
        // Calculate the exact angular distance from the needle to this LED
        float led_angle = led_idx * 30.0f;
        float diff = abs(north_angle - led_angle);
        if (diff > 180.0f) diff = 360.0f - diff; // shortest path wrapping
        
        // Map distance to brightness: 
        // 0 deg away = max brightness (255)
        // 30 deg away (next LED) = min brightness (0)
        float intensity = 1.0f - (diff / 30.0f);
        if (intensity < 0.0f) intensity = 0.0f;
        
        // Apply intensity to Red channel
        int red_val = round(255.0f * intensity);
        
        // If led_enabled, we draw it
        if (leds_enabled && red_val > 0) {
          strip.setPixelColor(led_idx, strip.Color(red_val, 0, 0));
        }
      }
      
      if (leds_enabled) {
        strip.show();
      }
    }
  }

  delay(20); // ~50Hz
}
