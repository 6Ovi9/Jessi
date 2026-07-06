#include <Wire.h>
#include <math.h>
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

bool writeReg(uint8_t reg, uint8_t val) {
  Wire.beginTransmission(sensor_address);
  Wire.write(reg);
  Wire.write(val);
  uint8_t err = Wire.endTransmission(true);
  if (err != 0) {
    Serial.print("  [ERROR] writeReg(0x");
    Serial.print(reg, HEX);
    Serial.print(") failed with err=");
    Serial.println(err);
    return false;
  }
  return true;
}

void setup() {
  Serial.begin(115200);
  while (!Serial && millis() < 5000);
  delay(1000);

  Serial.println("\n\n--- COMPASS DIAGNOSTIC TOOL v2 ---");
  Serial.println("Relax! The compass is NOT fried! It answered the I2C scan.");
  Serial.println("The issue is that SOFT_RST completely halts the chip when pullups are weak.");
  
  applyInternalPullups();
  delay(10);
  
  Wire.setPins(PIN_COMPASS_SDA, PIN_COMPASS_SCL);
  Wire.begin();
  applyInternalPullups(); // Must re-apply after begin()
  
  Wire.setClock(10000); // 10kHz for weak internal pullups
  Wire.setTimeout(15);
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

  Serial.println("\nPolling WHO_AM_I...");
  Wire.beginTransmission(sensor_address);
  Wire.write(0x0F); // WHO_AM_I
  Wire.endTransmission(true);
  Wire.requestFrom(sensor_address, (uint8_t)1);
  if (Wire.available()) {
    uint8_t who = Wire.read();
    Serial.print("  WHO_AM_I = 0x");
    Serial.println(who, HEX);
  } else {
    Serial.println("  Failed to read WHO_AM_I");
  }

  Serial.println("\nConfiguring registers (NO Soft Reset)...");
  
  // CTRL1
  if (writeReg(0x20, 0x70)) Serial.println("  CTRL1 OK (UHP, 10Hz)");
  delay(20);

  // CTRL2
  if (writeReg(0x21, 0x00)) Serial.println("  CTRL2 OK (+/- 4 Gauss)");
  delay(20);

  // CTRL4
  if (writeReg(0x23, 0x0C)) Serial.println("  CTRL4 OK (Z-axis UHP)");
  delay(20);

  // CTRL3 (Continuous mode)
  if (writeReg(0x22, 0x00)) Serial.println("  CTRL3 OK (Continuous Mode)");
  delay(200); // Wait for first sample to stabilize

  Serial.println("\nSetup Complete. Reading Data...\n");
}

void loop() {
  // Directly read 6 bytes from OUT_X_L with auto-increment
  // We skip polling STATUS_REG because back-to-back I2C reads with weak pullups cause collisions!
  Wire.beginTransmission(sensor_address);
  Wire.write(0x28 | 0x80); 
  uint8_t err = Wire.endTransmission(true);
  
  if (err == 0) {
    Wire.requestFrom(sensor_address, (uint8_t)6);
    if (Wire.available() == 6) {
      int16_t rx = (int16_t)(Wire.read() | (Wire.read() << 8));
      int16_t ry = (int16_t)(Wire.read() | (Wire.read() << 8));
      int16_t rz = (int16_t)(Wire.read() | (Wire.read() << 8));
      
      // Sensitivity at +/- 4 Gauss is 6842 LSB/Gauss (1 Gauss = 100 uT)
      float cx = rx * (100.0f / 6842.0f);
      float cy = ry * (100.0f / 6842.0f);
      float cz = rz * (100.0f / 6842.0f);
      
      float heading = atan2(cy, cx) * 180.0 / PI;
      if (heading < 0) heading += 360.0;
      
      Serial.print("Heading: ");
      Serial.print(heading, 1);
      Serial.print(" | X: ");
      Serial.print(cx, 2);
      Serial.print(" Y: ");
      Serial.print(cy, 2);
      Serial.print(" Z: ");
      Serial.println(cz, 2);
    } else {
      Serial.println("[ERROR] requestFrom did not return 6 bytes");
    }
  } else {
    Serial.print("[ERROR] Failed to send address for read, err=");
    Serial.println(err);
  }
  
  delay(100); // Poll at 10Hz
}
