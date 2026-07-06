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
  Wire.end(); delay(5);
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
  sda_low(); delayMicroseconds(HALF_US);
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
  scl_high(); bool ack = !sda_read(); scl_low();
  return ack;
}
uint8_t i2c_read_byte(bool send_ack) {
  uint8_t v = 0;
  sda_high();
  for (int i = 7; i >= 0; i--) {
    scl_high(); if (sda_read()) v |= (1 << i);
    scl_low(); delayMicroseconds(HALF_US / 4);
  }
  if (send_ack) sda_low(); else sda_high();
  delayMicroseconds(HALF_US / 4);
  scl_high(); scl_low(); sda_high();
  return v;
}

// Write register, with optional retry once
bool wreg(uint8_t reg, uint8_t val, bool retry = true) {
  for (int attempt = 0; attempt < (retry ? 3 : 1); attempt++) {
    if (attempt > 0) { delayMicroseconds(500); }
    i2c_start();
    bool a1 = i2c_write_byte(COMPASS_ADDR << 1);
    bool a2 = a1 && i2c_write_byte(reg);
    bool a3 = a2 && i2c_write_byte(val);
    i2c_stop();
    if (a3) return true;
  }
  return false;
}

// Read register (STOP+START, no repeated start)
bool rreg(uint8_t reg, uint8_t &val) {
  i2c_start();
  bool ok = i2c_write_byte(COMPASS_ADDR << 1) && i2c_write_byte(reg);
  i2c_stop();
  if (!ok) return false;
  delayMicroseconds(50);
  i2c_start();
  ok = i2c_write_byte((COMPASS_ADDR << 1) | 1);
  if (ok) val = i2c_read_byte(false);
  i2c_stop();
  return ok;
}

uint32_t loopCount = 0;
uint32_t failCount = 0;

void setup() {
  Serial.begin(115200);
  while (!Serial && millis() < 5000);
  delay(1000);
  Serial.println("\n\n--- COMPASS v9: No delay between conversions ---");
  i2c_init();

  // Scan
  for (uint8_t addr = 1; addr < 127; addr++) {
    i2c_start(); bool ok = i2c_write_byte(addr << 1); i2c_stop();
    if (ok) { Serial.print("Found 0x"); Serial.println(addr, HEX); }
    delayMicroseconds(100);
  }

  uint8_t who = 0;
  rreg(0x0F, who);
  Serial.print("WHO_AM_I = 0x"); Serial.println(who, HEX);
  Serial.println("Starting conversion loop (NO inter-measurement delay)...\n");
}

void loop() {
  loopCount++;

  // ── Trigger single conversion ──────────────────────────────────────────────
  if (!wreg(0x22, 0x01)) {
    failCount++;
    Serial.print("[FAIL] CTRL3 write failed. Loop=");
    Serial.print(loopCount);
    Serial.print(" Fails=");
    Serial.println(failCount);
    // Try WHO_AM_I to see if chip is even alive
    uint8_t who = 0;
    bool alive = rreg(0x0F, who);
    Serial.print("  WHO_AM_I check: ");
    Serial.println(alive ? (who == 0x3D ? "ALIVE (0x3D)" : "WRONG ID") : "DEAD");
    // Small recovery wait, try again
    delay(50);
    return;
  }

  // ── Wait for data ready ────────────────────────────────────────────────────
  uint32_t t0 = millis();
  bool drdy = false;
  while (millis() - t0 < 500) {
    uint8_t s = 0;
    if (rreg(0x27, s) && (s & 0x08)) { drdy = true; break; }
    delayMicroseconds(500); // very short poll interval
  }

  if (!drdy) {
    Serial.println("[WARN] DRDY timeout");
    return;
    // NO delay — immediately try next conversion
  }

  // ── Read X,Y,Z ─────────────────────────────────────────────────────────────
  uint8_t xl=0,xh=0,yl=0,yh=0,zl=0,zh=0;
  if (!(rreg(0x28,xl) && rreg(0x29,xh) &&
        rreg(0x2A,yl) && rreg(0x2B,yh) &&
        rreg(0x2C,zl) && rreg(0x2D,zh))) {
    Serial.println("[FAIL] Axis read failed");
    return;
    // NO delay — immediately try next conversion
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
  Serial.print("  Z: ");     Serial.print(rz * (100.0f / 6842.0f), 2);
  Serial.print("  [loop="); Serial.print(loopCount); Serial.println("]");

  // ── NO delay — immediately go back to top and trigger next conversion ──────
  // (only a tiny gap — the time it takes to call Serial.print above)
}
