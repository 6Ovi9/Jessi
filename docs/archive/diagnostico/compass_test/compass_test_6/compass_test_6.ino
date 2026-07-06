#include <Wire.h>
#include "variant.h"

#define PIN_COMPASS_SDA D4
#define PIN_COMPASS_SCL D5

uint8_t sensor_address = 0x1E;

// ─────────────────────────────────────────────────────────────────────────────
// BUS RECOVERY: called whenever the bus gets stuck (SDA held low by sensor).
// Bit-bangs up to 9 SCL pulses to force the LIS3MDL to release SDA, then
// sends a manual STOP condition before handing back to Wire.
// ─────────────────────────────────────────────────────────────────────────────
void busRecovery() {
  // Step 1: Hand pins back to GPIO (bypass the TWIM peripheral)
  Wire.end();
  delay(1);

  // Use direct NRF register writes so we don't disturb the TWI peripheral
  // SCL = output HIGH, SDA = input (let sensor drive it)
  uint32_t scl_pad = g_ADigitalPinMap[PIN_COMPASS_SCL];
  uint32_t sda_pad = g_ADigitalPinMap[PIN_COMPASS_SDA];
  NRF_GPIO_Type* scl_port = (scl_pad < 32) ? NRF_P0 : NRF_P1;
  NRF_GPIO_Type* sda_port = (sda_pad < 32) ? NRF_P0 : NRF_P1;
  uint32_t scl_bit = scl_pad & 0x1F;
  uint32_t sda_bit = sda_pad & 0x1F;

  // Configure SCL as output HIGH, open-drain (INPUT_PULLUP + drive HIGH trick)
  scl_port->OUTSET = (1UL << scl_bit);
  scl_port->PIN_CNF[scl_bit] = (GPIO_PIN_CNF_DIR_Output    << GPIO_PIN_CNF_DIR_Pos) |
                                (GPIO_PIN_CNF_INPUT_Connect << GPIO_PIN_CNF_INPUT_Pos) |
                                (GPIO_PIN_CNF_PULL_Pullup   << GPIO_PIN_CNF_PULL_Pos) |
                                (GPIO_PIN_CNF_DRIVE_S0D1    << GPIO_PIN_CNF_DRIVE_Pos); // open-drain

  // Configure SDA as input with pullup (let us read if sensor holds it low)
  sda_port->PIN_CNF[sda_bit] = (GPIO_PIN_CNF_DIR_Input     << GPIO_PIN_CNF_DIR_Pos) |
                                (GPIO_PIN_CNF_INPUT_Connect << GPIO_PIN_CNF_INPUT_Pos) |
                                (GPIO_PIN_CNF_PULL_Pullup   << GPIO_PIN_CNF_PULL_Pos);

  delayMicroseconds(50);

  // Step 2: Clock up to 9 SCL pulses until SDA is released
  Serial.print("[BUS] SDA before recovery: ");
  Serial.println((sda_port->IN >> sda_bit) & 1);

  for (int i = 0; i < 9; i++) {
    scl_port->OUTCLR = (1UL << scl_bit); // SCL LOW
    delayMicroseconds(50);
    scl_port->OUTSET = (1UL << scl_bit); // SCL HIGH
    delayMicroseconds(50);

    if ((sda_port->IN >> sda_bit) & 1) {
      Serial.print("[BUS] SDA released after ");
      Serial.print(i + 1);
      Serial.println(" pulses");
      break;
    }
  }

  Serial.print("[BUS] SDA after recovery: ");
  Serial.println((sda_port->IN >> sda_bit) & 1);

  // Step 3: Generate a STOP condition (SDA LOW → HIGH while SCL HIGH)
  // SDA low
  sda_port->OUTCLR = (1UL << sda_bit);
  sda_port->PIN_CNF[sda_bit] = (GPIO_PIN_CNF_DIR_Output    << GPIO_PIN_CNF_DIR_Pos) |
                                (GPIO_PIN_CNF_INPUT_Connect << GPIO_PIN_CNF_INPUT_Pos) |
                                (GPIO_PIN_CNF_PULL_Pullup   << GPIO_PIN_CNF_PULL_Pos) |
                                (GPIO_PIN_CNF_DRIVE_S0D1    << GPIO_PIN_CNF_DRIVE_Pos); // open-drain
  delayMicroseconds(50);
  // SCL high
  scl_port->OUTSET = (1UL << scl_bit);
  delayMicroseconds(50);
  // SDA high → STOP
  sda_port->OUTSET = (1UL << sda_bit);
  delayMicroseconds(50);

  // Step 4: Re-initialize Wire (TWIM takes over the pins again)
  Wire.setPins(PIN_COMPASS_SDA, PIN_COMPASS_SCL);
  Wire.begin();

  // Step 5: Re-assert internal pullups (Wire.begin resets pin config)
  sda_port->PIN_CNF[sda_bit] = (sda_port->PIN_CNF[sda_bit] & ~GPIO_PIN_CNF_PULL_Msk) |
                                (GPIO_PIN_CNF_PULL_Pullup << GPIO_PIN_CNF_PULL_Pos);
  scl_port->PIN_CNF[scl_bit] = (scl_port->PIN_CNF[scl_bit] & ~GPIO_PIN_CNF_PULL_Msk) |
                                (GPIO_PIN_CNF_PULL_Pullup << GPIO_PIN_CNF_PULL_Pos);

  Wire.setClock(10000);
  Wire.setTimeout(15);
  delayMicroseconds(200);
}

void setup() {
  Serial.begin(115200);
  while (!Serial && millis() < 5000);
  delay(1000);

  Serial.println("\n\n--- COMPASS DIAGNOSTIC TOOL v6 (Bus Recovery) ---");

  busRecovery(); // Start clean
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
    Serial.println("No compass found!");
    while (1) delay(1000);
  }

  Serial.println("\nTesting WHO_AM_I loop with bus recovery on each error...");
}

void loop() {
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
      Serial.println("[WARN] No bytes from requestFrom — recovering...");
      busRecovery();
    }
  } else {
    Serial.print("[ERROR] err=");
    Serial.print(err);
    Serial.println(" — running bus recovery...");
    busRecovery();
  }

  delay(100);
}
