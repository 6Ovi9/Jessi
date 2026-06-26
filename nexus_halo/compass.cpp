#include "compass.h"
#include <InternalFileSystem.h>
#include <cmath>

using namespace Adafruit_LittleFS_Namespace;

// ============================================================================
// CONSTRUCTOR
// ============================================================================

CompassController::CompassController()
    : sensor_connected(false), sensor_address(0x00), zero_reading_count(0),
      current_heading(0.0f), heading_filtered(0.0f), last_update_ms(0),
      calibrating(false), calibration_start_ms(0), min_x(0), max_x(0), min_y(0),
      max_y(0), min_z(0), max_z(0), offset_x(COMPASS_HARD_IRON_X),
      offset_y(COMPASS_HARD_IRON_Y), offset_z(COMPASS_HARD_IRON_Z),
      scale_x(COMPASS_SOFT_IRON_XX), scale_y(COMPASS_SOFT_IRON_YY),
      scale_z(COMPASS_SOFT_IRON_ZZ), raw_x(0.0f), raw_y(0.0f), raw_z(0.0f),
      history_idx(0), history_filled(false) {
  memset(history_x, 0, sizeof(history_x));
  memset(history_y, 0, sizeof(history_y));
}

// ============================================================================
// I2C LOW-LEVEL HELPERS
// ============================================================================

bool CompassController::_writeReg(uint8_t reg, uint8_t value) {
  Wire.beginTransmission(sensor_address);
  Wire.write(reg);
  Wire.write(value);
  return (Wire.endTransmission(true) == 0);
}

bool CompassController::_readReg(uint8_t reg, uint8_t &value) {
  Wire.beginTransmission(sensor_address);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0)
    return false;
  Wire.requestFrom(sensor_address, (uint8_t)1);
  if (!Wire.available())
    return false;
  value = Wire.read();
  return true;
}

// Lee los 6 bytes de datos XYZ directamente por I2C.
// El LIS3MDL soporta auto-increment con el bit7 del registro a 1.
bool CompassController::_readRaw(float &x, float &y, float &z) {
  Wire.beginTransmission(sensor_address);
  Wire.write(LIS3MDL_REG_OUT_X_L | 0x80); // bit7=1 → auto-increment
  if (Wire.endTransmission(false) != 0)
    return false;

  Wire.requestFrom(sensor_address, (uint8_t)6);
  if (Wire.available() < 6)
    return false;

  int16_t rx = (int16_t)(Wire.read() | ((uint16_t)Wire.read() << 8));
  int16_t ry = (int16_t)(Wire.read() | ((uint16_t)Wire.read() << 8));
  int16_t rz = (int16_t)(Wire.read() | ((uint16_t)Wire.read() << 8));

  // Convertir a µT: sensibilidad ±4G = 6842 LSB/Gauss, 1 Gauss = 100 µT
  const float scale = 100.0f / LIS3MDL_SENSITIVITY;
  x = rx * scale;
  y = ry * scale;
  z = rz * scale;
  return true;
}

bool CompassController::_softReset() {
  // CTRL_REG2 bit2 = SOFT_RST
  Wire.beginTransmission(sensor_address);
  Wire.write(LIS3MDL_REG_CTRL2);
  Wire.write(0x04);
  if (Wire.endTransmission(true) != 0)
    return false;
  delay(20); // Datasheet: esperar al menos 10 ms tras reset
  return true;
}

bool CompassController::_configure() {
  // CTRL_REG1: TEMP_EN=0, XY medium performance (0b01), ODR=10Hz (0b100),
  // FAST_ODR=0, ST=0
  //   bits: [TEMP_EN | OM1 | OM0 | DO2 | DO1 | DO0 | FAST_ODR | ST]
  //   OM=01 (medium), DO=100 (10Hz) → 0b0_01_100_0_0 = 0x30...
  //   Datasheet tabla: OM=01, DO=100 → CTRL_REG1 = 0b01000000 | 0b00001000 =
  //   nope Calculado: TEMP_EN=0, OM[1:0]=01, DO[2:0]=100, FAST_ODR=0, ST=0 = 0
  //   | (01<<5) | (100<<2) | 0 | 0 = 0x20 | 0x10 = 0x30... Correcto:
  //   bits[6:5]=OM=01=0b01, bits[4:2]=DO=100=0b100 CTRL_REG1 = 0b0_01_100_00 =
  //   0x30  ← medium perf, 10Hz
  if (!_writeReg(LIS3MDL_REG_CTRL1, 0x30))
    return false;

  // CTRL_REG2: FS=01 (±4 Gauss), REBOOT=0, SOFT_RST=0
  //   bits[6:5]=FS → 01 → 0x20
  if (!_writeReg(LIS3MDL_REG_CTRL2, 0x20))
    return false;

  // CTRL_REG3: modo operación continua → 0x00
  //   MD[1:0]=00 → continuous conversion
  if (!_writeReg(LIS3MDL_REG_CTRL3, 0x00))
    return false;

  // CTRL_REG4: Z medium performance (OMZ=01), little-endian
  //   bits[3:2]=OMZ=01 → 0x04
  if (!_writeReg(LIS3MDL_REG_CTRL4, 0x04))
    return false;

  // CTRL_REG5: BDU=1 (block data update, evita leer datos a mitad de
  // conversión)
  //   bit6=BDU → 0x40
  if (!_writeReg(LIS3MDL_REG_CTRL5, 0x40))
    return false;

  return true;
}

bool CompassController::_resetBus() {
  Wire.end();
  delay(10);
  Wire.setPins(PIN_COMPASS_SDA, PIN_COMPASS_SCL);
  Wire.begin();
  Wire.setClock(10000); // 10kHz to match setup() and prevent weak pull-up issues
  delay(20);
  return true;
}

// ============================================================================
// BEGIN
// ============================================================================

void CompassController::begin() {
  Serial.println("[COMPASS] Initializing Wire (D4=SDA, D5=SCL)...");

  // Wire ya está inicializado por setup() antes de llamar a begin().
  // Solo probamos las dos direcciones posibles del LIS3MDL.

  const uint8_t candidates[] = {0x1C, 0x1E};
  uint8_t addr_found = 0x00;

  for (uint8_t i = 0; i < 2; i++) {
    uint8_t addr = candidates[i];
    uint8_t who = 0x00;

    Wire.beginTransmission(addr);
    Wire.write(LIS3MDL_REG_WHO_AM_I);
    int err = Wire.endTransmission(true);

    if (err != 0) {
      Serial.print("[COMPASS] Direct read from 0x");
      Serial.print(addr, HEX);
      Serial.print(" failed (error=");
      Serial.print(err);
      Serial.println(").");
      continue;
    }

    Wire.requestFrom(addr, (uint8_t)1);
    if (Wire.available()) {
      who = Wire.read();
      Serial.print("[COMPASS] Direct WHO_AM_I read from 0x");
      Serial.print(addr, HEX);
      Serial.print(" returned: 0x");
      Serial.println(who, HEX);
    }

    if (who == LIS3MDL_WHO_AM_I_VALUE) {
      addr_found = addr;
      break;
    }
  }

  if (addr_found == 0x00) {
    Serial.println("[COMPASS] ✗ LIS3MDL not found at 0x1C or 0x1E on Wire. "
                   "Check D4/D5 wiring. Relying on app bearing.");
    sensor_connected = false;
    return;
  }

  sensor_address = addr_found;

  // Reset software para limpiar cualquier estado previo
  _softReset();

  // Configurar registros de control
  if (!_configure()) {
    Serial.println("[COMPASS] ✗ Failed to configure LIS3MDL registers.");
    sensor_connected = false;
    return;
  }

  // Volcar registros para diagnóstico
  Serial.println("[COMPASS] DUMPING CTRL REGISTERS:");
  for (uint8_t reg = 0x20; reg <= 0x24; reg++) {
    uint8_t val = 0;
    _readReg(reg, val);
    Serial.print("  CTRL_REG");
    Serial.print(reg - 0x20 + 1);
    Serial.print(" (0x");
    Serial.print(reg, HEX);
    Serial.print(") = 0x");
    Serial.println(val, HEX);
  }

  sensor_connected = true;
  Serial.print("[COMPASS] ✓ LIS3MDL found on Wire (D4/D5) at 0x");
  Serial.println(sensor_address, HEX);

  // Esperar primera muestra (ODR=10Hz → 100ms, damos margen)
  delay(150);

  // Verificar que hay datos
  float fx, fy, fz;
  if (_readRaw(fx, fy, fz)) {
    Serial.print("[COMPASS] First sample — X=");
    Serial.print(fx, 2);
    Serial.print(" Y=");
    Serial.print(fy, 2);
    Serial.print(" Z=");
    Serial.print(fz, 2);
    Serial.println(" µT");

    if (fx == 0.0f && fy == 0.0f && fz == 0.0f) {
      Serial.println("[COMPASS] ⚠ All zeros — chip may not be in continuous "
                     "mode yet. Will retry in loop.");
    } else {
      raw_x = fx;
      raw_y = fy;
      raw_z = fz;
    }
  } else {
    Serial.println("[COMPASS] ⚠ First read failed. Will retry in loop.");
  }

  // Cargar calibración desde flash (si existe)
  loadCalibration();
}

// ============================================================================
// UPDATE (llamar desde loop)
// ============================================================================

void CompassController::update() {
  if (!sensor_connected)
    return;

  uint32_t now_ms = millis();

  // Limitar a COMPASS_UPDATE_RATE_HZ
  if ((now_ms - last_update_ms) < (1000u / COMPASS_UPDATE_RATE_HZ))
    return;
  last_update_ms = now_ms;

  // ── Leer datos crudos ──────────────────────────────────────────────────────
  float rx, ry, rz;
  bool ok = _readRaw(rx, ry, rz);

  // Debug print cada 1 segundo
  /*static uint32_t last_debug_ms = 0;
  bool do_debug = (now_ms - last_debug_ms >= 1000);
  if (do_debug) {
    last_debug_ms = now_ms;
    Serial.print("[COMPASS DEBUG] Raw: X=");
    Serial.print(ok ? rx : raw_x, 2);
    Serial.print(" Y=");
    Serial.print(ok ? ry : raw_y, 2);
    Serial.print(" Z=");
    Serial.print(ok ? rz : raw_z, 2);
    Serial.print(" | Status: ");
    Serial.println(ok ? "OK" : "FAIL");
  }*/

  if (ok && !(rx == 0.0f && ry == 0.0f && rz == 0.0f)) {
    raw_x = rx;
    raw_y = ry;
    raw_z = rz;
    zero_reading_count = 0;
  } else {
    zero_reading_count++;

    if (zero_reading_count >= 3) {
      Serial.println(
          "[COMPASS] ⚠ 3 consecutive failed/zero reads. Re-initializing...");

      if (_resetBus()) {
        _softReset();
        if (_configure()) {
          Serial.println("[COMPASS] ✓ Sensor re-configured after bus reset.");
        } else {
          Serial.println("[COMPASS] ✗ Re-configure failed. Disabling compass.");
          sensor_connected = false;
          return;
        }
      }
      zero_reading_count = 0;
    }
    // Usar último valor conocido para el cálculo
  }

  // ── Calibración en tiempo real (si activa) ─────────────────────────────────
  if (calibrating)
    calibrationUpdate();

  // ── Aplicar calibración a copias ──────────────────────────────────────────
  float cx = raw_x;
  float cy = raw_y;
  float cz = raw_z;
  _applyCalibration(cx, cy, cz);

  // ── Filtro de mediana (ventana 5) ──────────────────────────────────────────
  history_x[history_idx] = cx;
  history_y[history_idx] = cy;
  history_idx++;
  if (history_idx >= FILTER_WINDOW) {
    history_idx = 0;
    history_filled = true;
  }

  float filtered_cx = cx;
  float filtered_cy = cy;

  if (history_filled) {
    float sx[FILTER_WINDOW], sy[FILTER_WINDOW];
    for (int i = 0; i < FILTER_WINDOW; i++) {
      sx[i] = history_x[i];
      sy[i] = history_y[i];
    }

    // Bubble sort simple
    for (int i = 0; i < FILTER_WINDOW - 1; i++) {
      for (int j = i + 1; j < FILTER_WINDOW; j++) {
        if (sx[j] < sx[i]) {
          float t = sx[i];
          sx[i] = sx[j];
          sx[j] = t;
        }
        if (sy[j] < sy[i]) {
          float t = sy[i];
          sy[i] = sy[j];
          sy[j] = t;
        }
      }
    }
    // Media recortada: descarta min y max, promedia los 3 del centro
    filtered_cx = (sx[1] + sx[2] + sx[3]) / 3.0f;
    filtered_cy = (sy[1] + sy[2] + sy[3]) / 3.0f;
  }

  // ── Calcular heading ───────────────────────────────────────────────────────
  float heading = _calculateHeading(filtered_cx, filtered_cy);

  // Low-pass filter (camino angular más corto)
  float diff = heading - heading_filtered;
  if (diff > 180.0f)
    diff -= 360.0f;
  if (diff < -180.0f)
    diff += 360.0f;
  heading_filtered += diff * COMPASS_HEADING_ALPHA;

  // Normalizar a 0–360
  current_heading = fmod(heading_filtered + 360.0f, 360.0f);

  /*if (do_debug) {
    Serial.print("[COMPASS DEBUG] Calib: cx=");
    Serial.print(cx, 2);
    Serial.print(" cy=");
    Serial.print(cy, 2);
    Serial.print(" | Heading: ");
    Serial.println(current_heading, 2);
  }*/
}

// ============================================================================
// CÁLCULO DE HEADING
// ============================================================================

float CompassController::_calculateHeading(float x, float y) {
  // atan2(-y, x): eje X apunta al frente, eje Y apunta a la derecha
  float heading_rad = atan2(-y, x);
  float heading_deg = heading_rad * 180.0f / M_PI;
  return fmod(heading_deg + 360.0f, 360.0f);
}

// ============================================================================
// CALIBRACIÓN HARD-IRON / SOFT-IRON
// ============================================================================

void CompassController::_applyCalibration(float &x, float &y, float &z) {
  x = (x - offset_x) * scale_x;
  y = (y - offset_y) * scale_y;
  z = (z - offset_z) * scale_z;
}

void CompassController::startCalibration() {
  calibrating = true;
  calibration_start_ms = millis();
  min_x = max_x = raw_x;
  min_y = max_y = raw_y;
  min_z = max_z = raw_z;
  Serial.println(
      "[COMPASS] Calibration started — rotate the watch in all directions.");
}

void CompassController::calibrationUpdate() {
  if (!calibrating)
    return;

  if (raw_x < min_x)
    min_x = raw_x;
  if (raw_x > max_x)
    max_x = raw_x;
  if (raw_y < min_y)
    min_y = raw_y;
  if (raw_y > max_y)
    max_y = raw_y;
  if (raw_z < min_z)
    min_z = raw_z;
  if (raw_z > max_z)
    max_z = raw_z;

  if ((millis() - calibration_start_ms) > 10000) {
    calibrating = false;

    offset_x = (max_x + min_x) / 2.0f;
    offset_y = (max_y + min_y) / 2.0f;
    offset_z = (max_z + min_z) / 2.0f;

    float range_x = (max_x - min_x) / 2.0f;
    float range_y = (max_y - min_y) / 2.0f;
    float range_z = (max_z - min_z) / 2.0f;
    float max_range = max(range_x, max(range_y, range_z));

    scale_x = (range_x > 0) ? max_range / range_x : 1.0f;
    scale_y = (range_y > 0) ? max_range / range_y : 1.0f;
    scale_z = (range_z > 0) ? max_range / range_z : 1.0f;

    Serial.println("[COMPASS] Calibration done. Saving...");
    saveCalibration();
  }
}

// ============================================================================
// FLASH: SAVE / LOAD CALIBRACIÓN
// ============================================================================

void CompassController::saveCalibration() {
  struct CompassCalibData {
    uint16_t magic;
    float offset_x, offset_y, offset_z;
    float scale_x, scale_y, scale_z;
    uint8_t checksum;
  };

  CompassCalibData data;
  data.magic = 0xC0A5;
  data.offset_x = offset_x;
  data.offset_y = offset_y;
  data.offset_z = offset_z;
  data.scale_x = scale_x;
  data.scale_y = scale_y;
  data.scale_z = scale_z;

  uint8_t sum = 0;
  const uint8_t *ptr = (const uint8_t *)&data;
  for (size_t i = 0; i < sizeof(data) - 1; i++)
    sum += ptr[i];
  data.checksum = sum;

  InternalFS.remove("compass.dat");
  File file(InternalFS);
  if (file.open("compass.dat", FILE_O_WRITE)) {
    file.write((const uint8_t *)&data, sizeof(data));
    file.close();
    Serial.println("[COMPASS] Calibration saved to flash.");
  } else {
    Serial.println("[COMPASS] ✗ Failed to save calibration.");
  }
}

void CompassController::loadCalibration() {
  struct CompassCalibData {
    uint16_t magic;
    float offset_x, offset_y, offset_z;
    float scale_x, scale_y, scale_z;
    uint8_t checksum;
  };

  File file(InternalFS);
  if (!file.open("compass.dat", FILE_O_READ))
    return;

  CompassCalibData data;
  if (file.read(&data, sizeof(data)) != sizeof(data)) {
    file.close();
    return;
  }
  file.close();

  if (data.magic != 0xC0A5)
    return;

  uint8_t sum = 0;
  const uint8_t *ptr = (const uint8_t *)&data;
  for (size_t i = 0; i < sizeof(data) - 1; i++)
    sum += ptr[i];
  if (sum != data.checksum)
    return;

  offset_x = data.offset_x;
  offset_y = data.offset_y;
  offset_z = data.offset_z;
  scale_x = data.scale_x;
  scale_y = data.scale_y;
  scale_z = data.scale_z;

  Serial.println("[COMPASS] Calibration loaded from flash.");
}