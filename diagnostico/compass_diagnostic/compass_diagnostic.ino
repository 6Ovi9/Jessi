/*
  ============================================================================
  LIS3MDL I2C Diagnostic Sketch
  ============================================================================
  Conectar al Monitor Serie (115200 baudios) tras flashear.
  
  Qué hace:
  1. Escanea el bus Wire (D4/D5) buscando cualquier dispositivo I2C
  2. Intenta leer WHO_AM_I (reg 0x0F) en 0x1C y 0x1E
  3. Espera que el LIS3MDL devuelva 0x3D si está bien
  
  NOTA: Este sketch es completamente autónomo — no depende de ninguna
  librería ni del resto del firmware del reloj.
  ============================================================================
*/

#include <Wire.h>

// Pines del bus externo (brújula)
#define SDA_PIN  D4
#define SCL_PIN  D5

// Registro WHO_AM_I del LIS3MDL
#define LIS3MDL_REG_WHO_AM_I   0x0F
#define LIS3MDL_WHO_AM_I_VAL   0x3D  // Valor esperado

// Intentar leer un registro vía I2C raw
// Devuelve el byte leído, o -1 en error
int readRegister(uint8_t addr, uint8_t reg) {
  Wire.beginTransmission(addr);
  Wire.write(reg);
  int err = Wire.endTransmission(false);  // Repeated start
  if (err != 0) {
    return -1;  // NACK o error de bus
  }
  
  uint8_t n = Wire.requestFrom(addr, (uint8_t)1);
  if (n == 0 || !Wire.available()) {
    return -2;  // Sin datos
  }
  return Wire.read();
}

void setup() {
  Serial.begin(115200);
  while (!Serial && millis() < 5000) delay(10);
  delay(500);
  
  Serial.println("\n============================================");
  Serial.println("  LIS3MDL DIAGNOSTIC — Wire (D4=SDA, D5=SCL)");
  Serial.println("============================================\n");

  // ──────────────────────────────────────────
  // Inicializar bus I2C con pull-ups internas
  // ──────────────────────────────────────────
  Wire.setPins(SDA_PIN, SCL_PIN);
  Wire.begin();
  // Habilitar pull-ups internas del nRF52840
  pinMode(SDA_PIN, INPUT_PULLUP);
  pinMode(SCL_PIN, INPUT_PULLUP);
  // Velocidad conservadora
  Wire.setClock(100000);
  delay(100);  // Dar tiempo al bus para estabilizarse
  
  Serial.println("[BUS] Wire inicializado: D4=SDA, D5=SCL, 100kHz");
  Serial.println("[BUS] Pull-ups internas: ACTIVADAS\n");

  // ──────────────────────────────────────────
  // PASO 1: Escáner I2C completo (0x01–0x7F)
  // ──────────────────────────────────────────
  Serial.println("--- PASO 1: Escaneo I2C (0x01 a 0x7F) ---");
  int found = 0;
  for (uint8_t addr = 1; addr < 128; addr++) {
    Wire.beginTransmission(addr);
    int err = Wire.endTransmission();
    if (err == 0) {
      Serial.print("  [FOUND] Dispositivo en 0x");
      if (addr < 0x10) Serial.print("0");
      Serial.println(addr, HEX);
      found++;
    }
  }
  if (found == 0) {
    Serial.println("  [ERROR] No se encontro ningun dispositivo I2C en el bus.");
    Serial.println("          => Revisar soldadura, alimentacion VDD/VDD_IO del LIS3MDL");
    Serial.println("          => Revisar los puentes en D4 y D5");
  }
  Serial.println();

  // ──────────────────────────────────────────
  // PASO 2: WHO_AM_I en 0x1C (SA1=GND)
  // ──────────────────────────────────────────
  Serial.println("--- PASO 2: WHO_AM_I en 0x1C (SA1 = GND) ---");
  int val_1C = readRegister(0x1C, LIS3MDL_REG_WHO_AM_I);
  if (val_1C < 0) {
    Serial.print("  [NACK] No responde en 0x1C (error=");
    Serial.print(val_1C);
    Serial.println(")");
  } else {
    Serial.print("  [OK]   WHO_AM_I en 0x1C = 0x");
    Serial.print(val_1C, HEX);
    if (val_1C == LIS3MDL_WHO_AM_I_VAL) {
      Serial.println("  ← CORRECTO! LIS3MDL encontrado.");
    } else {
      Serial.println("  ← INCORRECTO. Esperado: 0x3D");
    }
  }
  Serial.println();
  
  // ──────────────────────────────────────────
  // PASO 3: WHO_AM_I en 0x1E (SA1=VDD)
  // ──────────────────────────────────────────
  Serial.println("--- PASO 3: WHO_AM_I en 0x1E (SA1 = VDD) ---");
  int val_1E = readRegister(0x1E, LIS3MDL_REG_WHO_AM_I);
  if (val_1E < 0) {
    Serial.print("  [NACK] No responde en 0x1E (error=");
    Serial.print(val_1E);
    Serial.println(")");
  } else {
    Serial.print("  [OK]   WHO_AM_I en 0x1E = 0x");
    Serial.print(val_1E, HEX);
    if (val_1E == LIS3MDL_WHO_AM_I_VAL) {
      Serial.println("  ← CORRECTO! LIS3MDL encontrado.");
    } else {
      Serial.println("  ← INCORRECTO. Esperado: 0x3D");
    }
  }
  Serial.println();

  // ──────────────────────────────────────────
  // PASO 4: Diagnóstico con vel. más lenta
  // ──────────────────────────────────────────
  Serial.println("--- PASO 4: Reintento a 10 kHz (pull-ups debiles?) ---");
  Wire.setClock(10000);  // Muy lento para compensar pull-ups débiles
  delay(20);
  
  uint8_t addrs[2] = {0x1C, 0x1E};
  for (uint8_t i = 0; i < 2; i++) {
    uint8_t addr = addrs[i];
    int val = readRegister(addr, LIS3MDL_REG_WHO_AM_I);
    Serial.print("  0x");
    if (addr < 0x10) Serial.print("0");
    Serial.print(addr, HEX);
    Serial.print(" @ 10kHz: ");
    if (val == LIS3MDL_WHO_AM_I_VAL) {
      Serial.println("0x3D <- FUNCIONA! Pull-ups demasiado debiles para 100kHz.");
    } else if (val < 0) {
      Serial.println("NACK");
    } else {
      Serial.print("0x");
      Serial.print(val, HEX);
      Serial.println(" (inesperado)");
    }
  }

  Serial.println();
  Serial.println("============================================");
  Serial.println("  Diagnostico completo.");
  Serial.println("============================================");
}

void loop() {
  // Nada en el loop — solo ejecutar setup una vez
  delay(10000);
}
