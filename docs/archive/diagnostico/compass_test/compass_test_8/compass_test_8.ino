#include <Arduino.h>
#include <Adafruit_TinyUSB.h>
#include <Wire.h>
#include <math.h>
#include "variant.h"

#define PIN_SDA D4
#define PIN_SCL D5
#define COMPASS_ADDR 0x1E

NRF_GPIO_Type* sclPort;
NRF_GPIO_Type* sdaPort;
uint32_t       sclBit;
uint32_t       sdaBit;

#define HALF_US 60
#define SCL_TIMEOUT_US 2000

static inline void scl_low()  { sclPort->OUTCLR = (1UL << sclBit); delayMicroseconds(HALF_US); }
static inline void sda_low()  { sdaPort->OUTCLR = (1UL << sdaBit); }
static inline void sda_high() { sdaPort->OUTSET = (1UL << sdaBit); }
static inline bool sda_read() { return (bool)((sdaPort->IN >> sdaBit) & 1); }
static inline void scl_high() {
  sclPort->OUTSET = (1UL << sclBit);
  uint32_t t = micros();
  while (!((sclPort->IN >> sclBit) & 1) && (micros() - t < SCL_TIMEOUT_US));
  delayMicroseconds(HALF_US);
}

void i2c_init() {
  Wire.end(); delay(5); // Kill TWIM peripheral completely

  uint32_t scl_pad = g_ADigitalPinMap[PIN_SCL];
  uint32_t sda_pad = g_ADigitalPinMap[PIN_SDA];
  sclPort = (scl_pad < 32) ? NRF_P0 : NRF_P1;
  sdaPort = (sda_pad < 32) ? NRF_P0 : NRF_P1;
  sclBit  = scl_pad & 0x1F;
  sdaBit  = sda_pad & 0x1F;

  uint32_t od = (GPIO_PIN_CNF_DIR_Output    << GPIO_PIN_CNF_DIR_Pos)  |
                (GPIO_PIN_CNF_INPUT_Connect << GPIO_PIN_CNF_INPUT_Pos) |
                (GPIO_PIN_CNF_PULL_Pullup   << GPIO_PIN_CNF_PULL_Pos)  |
                (GPIO_PIN_CNF_DRIVE_S0D1    << GPIO_PIN_CNF_DRIVE_Pos);

  sclPort->OUTSET = (1UL << sclBit);
  sdaPort->OUTSET = (1UL << sdaBit);
  sclPort->PIN_CNF[sclBit] = od;
  sdaPort->PIN_CNF[sdaBit] = od;
  delayMicroseconds(500);
}

void i2c_start() {
  sda_high(); scl_high();
  delayMicroseconds(HALF_US);
  sda_low();
  delayMicroseconds(HALF_US);
  scl_low();
}

void i2c_stop() {
  sda_low(); delayMicroseconds(HALF_US / 2);
  scl_high(); delayMicroseconds(HALF_US);
  sda_high(); delayMicroseconds(HALF_US);
}

bool i2c_write_byte(uint8_t b) {
  for (int i = 7; i >= 0; i--) {
    if ((b >> i) & 1) sda_high(); else sda_low();
    delayMicroseconds(HALF_US / 4);
    scl_high(); scl_low();
  }
  sda_high(); delayMicroseconds(HALF_US / 4);
  scl_high();
  bool ack = !sda_read();
  scl_low();
  return ack;
}

uint8_t i2c_read_byte(bool send_ack) {
  uint8_t v = 0;
  sda_high();
  for (int i = 7; i >= 0; i--) {
    scl_high();
    if (sda_read()) v |= (1 << i);
    scl_low(); delayMicroseconds(HALF_US / 4);
  }
  if (send_ack) sda_low(); else sda_high();
  delayMicroseconds(HALF_US / 4);
  scl_high(); scl_low(); sda_high();
  return v;
}

// Write single register
bool wreg(uint8_t reg, uint8_t val) {
  i2c_start();
  if (!i2c_write_byte(COMPASS_ADDR << 1)) { i2c_stop(); return false; }
  if (!i2c_write_byte(reg))               { i2c_stop(); return false; }
  if (!i2c_write_byte(val))               { i2c_stop(); return false; }
  i2c_stop();
  return true;
}

// Read single register — SEPARATE STOP+START (no repeated start, avoids the TWIM confusion entirely)
bool rreg(uint8_t reg, uint8_t &val) {
  // Phase 1: write register address, then FULL STOP
  i2c_start();
  if (!i2c_write_byte(COMPASS_ADDR << 1)) { i2c_stop(); return false; }
  if (!i2c_write_byte(reg))               { i2c_stop(); return false; }
  i2c_stop();
  delayMicroseconds(100); // Let bus fully settle before next START

  // Phase 2: fresh START, read 1 byte
  i2c_start();
  if (!i2c_write_byte((COMPASS_ADDR << 1) | 1)) { i2c_stop(); return false; }
  val = i2c_read_byte(false); // NAK
  i2c_stop();
  return true;
}

void setup() {
  Serial.begin(115200);
  while (!Serial && millis() < 5000);
  delay(1000);

  Serial.println("\n\n--- COMPASS DIAGNOSTIC TOOL v8 ---");
  Serial.println("Single-conversion mode. No burst. STOP+START instead of Repeated START.");

  i2c_init();

  // Scan
  Serial.println("\nScanning...");
  for (uint8_t addr = 1; addr < 127; addr++) {
    i2c_start();
    bool ok = i2c_write_byte(addr << 1);
    i2c_stop();
    if (ok) { Serial.print("  Found 0x"); Serial.println(addr, HEX); }
    delayMicroseconds(100);
  }

  // WHO_AM_I
  uint8_t who = 0;
  Serial.print("WHO_AM_I = ");
  if (rreg(0x0F, who)) { Serial.print("0x"); Serial.println(who, HEX); }
  else                  Serial.println("FAILED");

  Serial.println("Ready. Starting single-conversion loop...\n");
}

void loop() {
  // ── Trigger a SINGLE conversion (low power, less current spike than continuous) ──
  if (!wreg(0x22, 0x01)) { // CTRL3 = 0x01 = Single conversion
    Serial.println("[ERROR] Could not write CTRL3");
    delay(500); return;
  }

  // ── Wait for ZYXDA (data ready) bit in STATUS_REG — timeout 200ms ──
  uint32_t t0 = millis();
  bool data_ready = false;
  while (millis() - t0 < 200) {
    uint8_t status = 0;
    if (rreg(0x27, status)) {
      if (status & 0x08) { data_ready = true; break; }
    }
    delay(5);
  }

  if (!data_ready) {
    Serial.println("[WARN] STATUS_REG ZYXDA never went HIGH — sensor may be browning out.");
    delay(500); return;
  }

  // ── Read X, Y, Z as individual single-byte reads ──
  uint8_t xl=0, xh=0, yl=0, yh=0, zl=0, zh=0;
  bool ok = rreg(0x28, xl) && rreg(0x29, xh) &&
            rreg(0x2A, yl) && rreg(0x2B, yh) &&
            rreg(0x2C, zl) && rreg(0x2D, zh);

  if (!ok) {
    Serial.println("[ERROR] Failed reading axis registers");
    delay(500); return;
  }

  int16_t rx = (int16_t)(xl | ((uint16_t)xh << 8));
  int16_t ry = (int16_t)(yl | ((uint16_t)yh << 8));
  int16_t rz = (int16_t)(zl | ((uint16_t)zh << 8));

  float cx = rx * (100.0f / 6842.0f);
  float cy = ry * (100.0f / 6842.0f);
  float heading = atan2(cy, cx) * 180.0f / PI;
  if (heading < 0) heading += 360.0f;

  Serial.print("Heading: "); Serial.print(heading, 1);
  Serial.print("  X: ");     Serial.print(cx, 2);
  Serial.print("  Y: ");     Serial.print(cy, 2);
  Serial.print("  Z: ");     Serial.println(rz * (100.0f / 6842.0f), 2);

  delay(200);
}
