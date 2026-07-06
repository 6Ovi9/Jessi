#include <Wire.h>
#include "variant.h"

#define PIN_COMPASS_SDA D4
#define PIN_COMPASS_SCL D5

uint8_t sensor_address = 0x1E;

void applyInternalPullups() {
  uint32_t sda_pin = g_ADigitalPinMap[PIN_COMPASS_SDA];
  NRF_GPIO_Type *sda_port = (sda_pin < 32) ? NRF_P0 : NRF_P1;
  sda_port->PIN_CNF[sda_pin & 0x1F] = (sda_port->PIN_CNF[sda_pin & 0x1F] & ~GPIO_PIN_CNF_PULL_Msk) | (GPIO_PIN_CNF_PULL_Pullup << GPIO_PIN_CNF_PULL_Pos);

  uint32_t scl_pin = g_ADigitalPinMap[PIN_COMPASS_SCL];
  NRF_GPIO_Type *scl_port = (scl_pin < 32) ? NRF_P0 : NRF_P1;
  scl_port->PIN_CNF[scl_pin & 0x1F] = (scl_port->PIN_CNF[scl_pin & 0x1F] & ~GPIO_PIN_CNF_PULL_Msk) | (GPIO_PIN_CNF_PULL_Pullup << GPIO_PIN_CNF_PULL_Pos);
}

void setupI2C() {
  Wire.end();
  delay(2);
  
  // Force bus to known idle state
  pinMode(PIN_COMPASS_SDA, INPUT_PULLUP);
  pinMode(PIN_COMPASS_SCL, INPUT_PULLUP);
  delay(2);

  Wire.setPins(PIN_COMPASS_SDA, PIN_COMPASS_SCL);
  Wire.begin();
  applyInternalPullups();
  Wire.setClock(10000); // 10kHz
  Wire.setTimeout(15);
}

void setup() {
  Serial.begin(115200);
  while (!Serial && millis() < 5000);
  delay(1000);

  Serial.println("\n\n--- COMPASS DIAGNOSTIC TOOL v5 ---");
  
  setupI2C();
  delay(50);

  Serial.println("\nScanning I2C bus...");
  bool found = false;
  for (uint8_t addr = 1; addr < 127; addr++) {
    Wire.beginTransmission(addr);
    if (Wire.endTransmission(true) == 0) {
      Serial.print("  Found device at 0x");
      Serial.println(addr, HEX);
      if (addr == 0x1C || addr == 0x1E) {
        sensor_address = addr;
        found = true;
      }
    }
  }

  if (!found) {
    Serial.println("No compass found! Check wiring.");
    while (1) delay(1000);
  }

  Serial.println("\nTesting WHO_AM_I loop with TWI RESET every loop...");
}

void loop() {
  setupI2C(); // RESET THE TWI HARDWARE EVERY LOOP
  delay(5);

  Wire.beginTransmission(sensor_address);
  Wire.write(0x0F); // WHO_AM_I
  
  uint8_t err = Wire.endTransmission(true); 
  
  if (err == 0) {
    Wire.requestFrom(sensor_address, (uint8_t)1);
    if (Wire.available()) {
      uint8_t who = Wire.read();
      Serial.print("WHO_AM_I = 0x");
      Serial.println(who, HEX);
    } else {
      Serial.println("[ERROR] requestFrom got no bytes");
    }
  } else {
    Serial.print("[ERROR] Failed to send address for WHO_AM_I, err=");
    Serial.println(err);
  }
  
  delay(100); // Poll at 10Hz
}
