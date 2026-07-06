#include <Arduino.h>
#include <Adafruit_TinyUSB.h>  // Required for Serial on Seeed nRF52 core
#include <Wire.h>              // Pulls in USB CDC Serial link target
#include <math.h>
#include "variant.h"

#define PIN_SDA D4
#define PIN_SCL D5
#define COMPASS_ADDR 0x1E

// ─────────────────────────────────────────────────────────────────────────────
// Raw GPIO handles (set once in i2c_init)
// ─────────────────────────────────────────────────────────────────────────────
NRF_GPIO_Type* sclPort;
NRF_GPIO_Type* sdaPort;
uint32_t       sclBit;
uint32_t       sdaBit;

#define HALF_PERIOD_US 60   // ~8 kHz effective — generous for weak pullups
#define SCL_RISE_TIMEOUT_US 2000

// ─────────────────────────────────────────────────────────────────────────────
// Open-drain helpers
// S0D1 = standard-0 / disconnect-1  →  true open-drain
// Drive LOW  → OUTCLR (pulls GND)
// Drive HIGH → OUTSET (disconnects, 13kΩ pullup raises the line)
// ─────────────────────────────────────────────────────────────────────────────
static inline void scl_low()  { sclPort->OUTCLR = (1UL << sclBit); delayMicroseconds(HALF_PERIOD_US); }
static inline void sda_low()  { sdaPort->OUTCLR = (1UL << sdaBit); }
static inline void sda_high() { sdaPort->OUTSET = (1UL << sdaBit); }
static inline bool sda_read() { return (bool)((sdaPort->IN >> sdaBit) & 1); }

static inline void scl_high() {
  sclPort->OUTSET = (1UL << sclBit);
  // Wait for SCL to actually reach HIGH (clock stretching + slow pullup rise)
  uint32_t t = micros();
  while (!((sclPort->IN >> sclBit) & 1) && (micros() - t < SCL_RISE_TIMEOUT_US));
  delayMicroseconds(HALF_PERIOD_US);
}

// ─────────────────────────────────────────────────────────────────────────────
// Init: configure both pins as open-drain output with internal pullup
// ─────────────────────────────────────────────────────────────────────────────
void i2c_init() {
  uint32_t scl_pad = g_ADigitalPinMap[PIN_SCL];
  uint32_t sda_pad = g_ADigitalPinMap[PIN_SDA];
  sclPort = (scl_pad < 32) ? NRF_P0 : NRF_P1;
  sdaPort = (sda_pad < 32) ? NRF_P0 : NRF_P1;
  sclBit  = scl_pad & 0x1F;
  sdaBit  = sda_pad & 0x1F;

  uint32_t od_cfg = (GPIO_PIN_CNF_DIR_Output    << GPIO_PIN_CNF_DIR_Pos)   |
                    (GPIO_PIN_CNF_INPUT_Connect  << GPIO_PIN_CNF_INPUT_Pos) |
                    (GPIO_PIN_CNF_PULL_Pullup    << GPIO_PIN_CNF_PULL_Pos)  |
                    (GPIO_PIN_CNF_DRIVE_S0D1     << GPIO_PIN_CNF_DRIVE_Pos);

  // Release both lines HIGH before configuring as open-drain
  sclPort->OUTSET = (1UL << sclBit);
  sdaPort->OUTSET = (1UL << sdaBit);
  sclPort->PIN_CNF[sclBit] = od_cfg;
  sdaPort->PIN_CNF[sdaBit] = od_cfg;
  delayMicroseconds(200);
}

// ─────────────────────────────────────────────────────────────────────────────
// I2C primitives
// ─────────────────────────────────────────────────────────────────────────────
void i2c_start() {
  sda_high(); scl_high();
  delayMicroseconds(HALF_PERIOD_US);
  sda_low();                          // SDA falls while SCL high → START
  delayMicroseconds(HALF_PERIOD_US);
  scl_low();
}

void i2c_stop() {
  sda_low();
  delayMicroseconds(HALF_PERIOD_US / 2);
  scl_high();
  delayMicroseconds(HALF_PERIOD_US);
  sda_high();                         // SDA rises while SCL high → STOP
  delayMicroseconds(HALF_PERIOD_US);
}

bool i2c_write_byte(uint8_t b) {
  for (int i = 7; i >= 0; i--) {
    if ((b >> i) & 1) sda_high(); else sda_low();
    delayMicroseconds(HALF_PERIOD_US / 4);
    scl_high();
    scl_low();
  }
  sda_high(); // release SDA for ACK
  delayMicroseconds(HALF_PERIOD_US / 4);
  scl_high();
  bool acked = !sda_read(); // LOW = ACK
  scl_low();
  return acked;
}

uint8_t i2c_read_byte(bool ack) {
  uint8_t val = 0;
  sda_high(); // release SDA so slave can drive
  for (int i = 7; i >= 0; i--) {
    scl_high();
    if (sda_read()) val |= (1 << i);
    scl_low();
    delayMicroseconds(HALF_PERIOD_US / 4);
  }
  if (ack) sda_low(); else sda_high();
  delayMicroseconds(HALF_PERIOD_US / 4);
  scl_high();
  scl_low();
  sda_high();
  return val;
}

// ─────────────────────────────────────────────────────────────────────────────
// High-level helpers
// ─────────────────────────────────────────────────────────────────────────────
bool i2c_read_reg(uint8_t addr, uint8_t reg, uint8_t& val) {
  i2c_start();
  if (!i2c_write_byte((addr << 1) | 0)) { i2c_stop(); return false; }
  if (!i2c_write_byte(reg))             { i2c_stop(); return false; }
  i2c_start(); // Repeated START — no STOP between write and read
  if (!i2c_write_byte((addr << 1) | 1)) { i2c_stop(); return false; }
  val = i2c_read_byte(false);
  i2c_stop();
  return true;
}

bool i2c_write_reg(uint8_t addr, uint8_t reg, uint8_t val) {
  i2c_start();
  if (!i2c_write_byte((addr << 1) | 0)) { i2c_stop(); return false; }
  if (!i2c_write_byte(reg))             { i2c_stop(); return false; }
  if (!i2c_write_byte(val))             { i2c_stop(); return false; }
  i2c_stop();
  return true;
}

bool i2c_read_burst(uint8_t addr, uint8_t reg, uint8_t* buf, uint8_t len) {
  i2c_start();
  if (!i2c_write_byte((addr << 1) | 0))  { i2c_stop(); return false; }
  if (!i2c_write_byte(reg | 0x80))       { i2c_stop(); return false; } // auto-increment
  i2c_start(); // Repeated START
  if (!i2c_write_byte((addr << 1) | 1))  { i2c_stop(); return false; }
  for (uint8_t i = 0; i < len; i++) {
    buf[i] = i2c_read_byte(i < len - 1); // ACK all but last
  }
  i2c_stop();
  return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// Setup
// ─────────────────────────────────────────────────────────────────────────────
void setup() {
  Serial.begin(115200);
  while (!Serial && millis() < 5000);
  delay(1000);

  Serial.println("\n\n--- COMPASS DIAGNOSTIC TOOL v7 (Pure Bit-Bang I2C) ---");
  Serial.println("Wire library completely bypassed. Using raw GPIO open-drain.");

  // Make sure Wire/TWIM peripheral is off so it doesn't fight our GPIO
  Wire.end();
  delay(5);

  i2c_init();
  delay(50);

  // Scan
  Serial.println("\nScanning...");
  for (uint8_t addr = 1; addr < 127; addr++) {
    i2c_start();
    bool acked = i2c_write_byte(addr << 1);
    i2c_stop();
    if (acked) {
      Serial.print("  Found device at 0x");
      Serial.println(addr, HEX);
    }
    delayMicroseconds(100);
  }

  // WHO_AM_I
  uint8_t who = 0;
  if (i2c_read_reg(COMPASS_ADDR, 0x0F, who)) {
    Serial.print("WHO_AM_I = 0x"); Serial.println(who, HEX);
  } else {
    Serial.println("WHO_AM_I read FAILED");
  }

  // CTRL3 = 0x00 → Continuous mode
  Serial.println("\nWriting CTRL3 (Continuous Mode)...");
  if (i2c_write_reg(COMPASS_ADDR, 0x22, 0x00)) {
    Serial.println("  CTRL3 OK");
  } else {
    Serial.println("  CTRL3 FAILED");
  }

  delay(200);
  Serial.println("\nReading heading in loop...\n");
}

// ─────────────────────────────────────────────────────────────────────────────
// Loop
// ─────────────────────────────────────────────────────────────────────────────
void loop() {
  uint8_t buf[6];
  if (i2c_read_burst(COMPASS_ADDR, 0x28, buf, 6)) {
    int16_t rx = (int16_t)(buf[0] | ((uint16_t)buf[1] << 8));
    int16_t ry = (int16_t)(buf[2] | ((uint16_t)buf[3] << 8));
    int16_t rz = (int16_t)(buf[4] | ((uint16_t)buf[5] << 8));

    float cx = rx * (100.0f / 6842.0f);
    float cy = ry * (100.0f / 6842.0f);
    float heading = atan2(cy, cx) * 180.0f / PI;
    if (heading < 0) heading += 360.0f;

    Serial.print("Heading: "); Serial.print(heading, 1);
    Serial.print("  | X: ");   Serial.print(cx, 2);
    Serial.print("  Y: ");     Serial.print(cy, 2);
    Serial.print("  Z: ");     Serial.println(rz * (100.0f / 6842.0f), 2);
  } else {
    Serial.println("[ERROR] Burst read failed");
  }

  delay(100);
}
