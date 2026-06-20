# Couples Watch Firmware v1.2

**Firmware para Seeed Studio XIAO nRF52840 Sense**

Smartwatch parejas con anillo LED de 12 posiciones, brújula, detección de gestos, vibración háptica, comunicación BLE, y calibración automática de rise-to-wake.

**Changes en v1.2**: Rise-to-wake con IMU motion detection + calibración automática por gestos + persistencia en flash.

---

## 📋 Contenido

```
firmware/
├── couples_watch.ino              # Main firmware (Arduino sketch) — v1.2
├── config.h                       # Global configuration & constants — v1.2
├── state_machine.h/.cpp           # Máquina de estados (11 estados) — v1.2
├── gesture.h/.cpp                 # Detección de gestos (TTP223)
├── led_controller.h/.cpp          # Control anillo SK6812 (12 LEDs)
├── compass.h/.cpp                 # LIS3MDL magnetómetro (brújula)
├── haptic.h/.cpp                  # Motor vibración (patrones)
├── ble_handler.h/.cpp             # Comunicación BLE + calibración — v1.2
├── power.h/.cpp                   # Power management + rise-to-wake — v1.2
├── imu_calibrator.h/.cpp          # NEW: Gesture-based threshold calc
├── eeprom_manager.h/.cpp          # NEW: Flash persistence + CRC
├── CHANGELOG_v1.2.md              # NEW: Detailed v1.2 changes
├── COMPILATION_VALIDATION.md      # NEW: Pre-compile checklist
└── README.md                      # Esta guía — v1.2
```

---

## ⚙️ Requisitos Hardware

| Componente | Modelo | Pin | Protocolo |
|---|---|---|---|
| Microcontrolador | Seeed XIAO nRF52840 Sense | — | BLE 5.0 + LSM6DS3 IMU |
| LEDs | 12× SK6812-MINI-E (RGB+W) | D7 (datos) | NeoPixel |
| Botón táctil | TTP223 | D8 | GPIO HIGH/LOW |
| Motor vibración | Motor + MOSFET | D9 | PWM |
| Magnetómetro | LIS3MDL | D4/D5 (I2C) | I2C custom |
| MOSFET LEDs | MOSFET gate | D10 | GPIO HIGH/LOW |
| IMU (integrado) | LSM6DS3TRC | D4/D5 + INT1 (P0.11) | I2C + INT |

---

## 📦 Librerías Arduino Requeridas

Instala en Arduino IDE: **Sketch → Include Library → Manage Libraries**

```
1. Adafruit_NeoPixel              → Control SK6812 LEDs
2. Adafruit_LIS3MDL               → Magnetómetro I2C
3. Adafruit_LSM6DS3TRC            → IMU integrado (rise-to-wake config)
4. ArduinoBLE                      → BLE (incluida con XIAO)
5. Adafruit_Sensor                → Sensor base library
6. RTClib (opcional)              → Reloj en tiempo real
```

**Instalación rápida:**
```bash
# Busca cada librería en el Manage Libraries y instala la versión más reciente
```

---

## 🔧 Configuración del Arduino IDE

### 1. Instala soporte para XIAO nRF52840

**Tools → Board Manager**, busca `Seeed nRF52 Boards`:

```
https://github.com/Seeed-Studio/Seeed_Platform/raw/master/package_seeeduino_boards_index.json
```

Instala la versión **2.9.1 o superior**.

### 2. Selecciona la placa

```
Tools → Board → Seeed nRF52 Boards → Seeed XIAO nRF52840 Sense
```

### 3. Configura el puerto COM

```
Tools → Port → COM[X] (Seeed XIAO nRF52840 Sense)
```

---

## 💾 Compilación y Carga

### Opción 1: Arduino IDE

1. **Abre** `couples_watch.ino` en Arduino IDE
2. **Verifica** (✓): `Sketch → Verify` — debe pasar sin errores
3. **Sube** (→): `Sketch → Upload`
4. **Monitor**: `Tools → Serial Monitor` (115200 baud) para ver debug

### Opción 2: Línea de comandos

```bash
# Compilar
arduino-cli compile --fqbn Seeeduino:nrf52:Seeeduino_nRF52840_Sense couples_watch.ino

# Cargar
arduino-cli upload --fqbn Seeeduino:nrf52:Seeeduino_nRF52840_Sense --port COM[X] couples_watch.ino
```

---

## ⚠️ CRÍTICO — v1.2: Rise-to-Wake Configuration

**En v1.2, el IMU LSM6DS3 ya NO se apaga completamente.** Esto permite la detección de movimiento para wake-on-motion.

### Power Consumption Trade-off

| Versión | IMU Estado | Deep Sleep | Ventaja |
|---------|------------|-----------|---------|
| v1.1 | Apagado | <10µA | Máxima duración batería |
| v1.2 | Bajo-poder 26Hz | ~30-35µA | Rise-to-wake activado |

### Configuración en `power.cpp`

```cpp
#define IMU_WAKE_ENABLED 1    // 1 = rise-to-wake ACTIVADO
                              // 0 = rise-to-wake DESACTIVADO (para testing)
```

Si `IMU_WAKE_ENABLED == 1`:
- LSM6DS3 configura a 26Hz (mínimo para wake-on-motion)
- INT1 pin (P0.11) genera interrupción por movimiento
- Threshold calibrado automáticamente por app

Si `IMU_WAKE_ENABLED == 0`:
- LSM6DS3 se apaga completamente (como v1.1)
- Solo botón D8 despierta el reloj
- Útil para testing sin batería crítica

### Calibración Automática (NUEVO en v1.2)

El usuario puede calibrar el threshold de motion-detection desde la app:

1. **App envía BLE comando**: "Calibration START"
2. **Reloj entra STATE_CALIBRATION_MODE**
3. **Usuario realiza 5 gestos de "levanta la muñeca"**
4. **LED anillo llena de 1 a 12 LEDs en progreso**
5. **App muestra: "2 / 5 gestos capturados"** via BLE
6. **Reloj calcula threshold automático** (mín aceleración × 0.8)
7. **Threshold se guarda en flash** (persiste entre reinicios)
8. **IMU se actualiza** con nuevo threshold
9. **LED anillo destella verde** + vibración de éxito
10. **Vuelve a reloj normal**

### EEPROM Storage (NUEVO en v1.2)

- **Ubicación**: nRF52840 Internal Flash (InternalFileSystem)
- **Formato**: `magic (0xCAFE) + threshold + timestamp + checksum`
- **Validación**: Bad magic/checksum → fallback a default (0x02)
- **Resiliencia**: CRC XOR para detectar corrupción

---

## ⚠️ CRÍTICO — Interrupt Handlers (v1.2)

Dos interrupciones despiertan el reloj desde deep sleep:

### 1. Button Tap (D8)
```cpp
void onButtonWakeup() {
  last_wake_source = WAKE_SOURCE_TAP;
}
```
- **Pin**: D8 (TTP223)
- **Evento**: RISING edge
- **Efecto**: Despierta reloj, entra CLOCK mode

### 2. Motion Detection (INT1) — NUEVO v1.2
```cpp
void onMotionWakeup() {
  last_wake_source = WAKE_SOURCE_MOTION;
}
```
- **Pin**: INT1 (P0.11, from LSM6DS3)
- **Evento**: RISING edge (aceleración > threshold)
- **Requisito**: `IMU_WAKE_ENABLED == 1`
- **Efecto**: Despierta reloj, entra CLOCK mode
- **Threshold**: Auto-calibrated, guardado en flash

---

## 🔧 Configuración del Arduino IDE (v1.2)

### 1. Instala soporte para XIAO nRF52840

**Tools → Board Manager**, busca `Seeed nRF52 Boards`:

```
https://github.com/Seeed-Studio/Seeed_Platform/raw/master/package_seeeduino_boards_index.json
```

Instala la versión **2.9.1 o superior**.

### 2. Selecciona la placa

```
Tools → Board → Seeed nRF52 Boards → Seeed XIAO nRF52840 Sense
```

### 3. Configura el puerto COM

```
Tools → Port → COM[X] (Seeed XIAO nRF52840 Sense)
```

### 4. Verifica la configuración de Upload Method

```
Tools → Upload Method → nRF52840 DK
```

---

## 💾 Compilación y Carga (v1.2)

### Opción 1: Arduino IDE

1. **Abre** `couples_watch.ino` en Arduino IDE
2. **Verifica** (✓): `Sketch → Verify` — debe pasar sin errores/warnings
3. **Sube** (→): `Sketch → Upload`
4. **Monitor**: `Tools → Serial Monitor` (115200 baud) para ver debug

**Tiempo de compilación**: ~30-60 segundos (nRF52 toolchain)

### Opción 2: Línea de comandos

```bash
# Compilar
arduino-cli compile --fqbn Seeeduino:nrf52:Seeeduino_nRF52840_Sense couples_watch.ino

# Cargar
arduino-cli upload --fqbn Seeeduino:nrf52:Seeeduino_nRF52840_Sense --port COM[X] couples_watch.ino
```

### Opción 3: Validación Pre-Compilación

Lee `COMPILATION_VALIDATION.md` para una checklist de 11 ítems antes de compilar.

---

## 🚀 Primer Arranque (v1.2)

1. **Carga el firmware** usando Arduino IDE
2. **Abre Serial Monitor** (115200 baud)
3. **Observa los logs**:
   ```
   [SETUP] Couples Watch Firmware v1.2
   [SETUP] Initializing...
   [SETUP] Initializing EEPROM...
   [SETUP] Powering down internal sensors...
   [SETUP] Rise-to-wake enabled. Loading calibration...
   [SETUP] Loaded threshold: 0x02    (o tu threshold guardado)
   [SETUP] Initializing gesture detector...
   [SETUP] Initializing LED ring...
   [SETUP] Initializing compass (LIS3MDL)...
   [SETUP] Initializing haptic motor...
   [SETUP] Initializing BLE...
   [SETUP] Initializing state machine...
   [SETUP] Motion wake interrupt attached
   [SETUP] Complete! Starting main loop...
   ```

4. **El reloj debe**:
   - Encender el anillo LED (color azul = sin BLE)
   - Mostrar la hora (3 agujas en LEDs)
   - Responder a toques en botón D8 (wake de deep sleep)
   - Responder a movimiento (INT1) si está cerca (requiere calibración)

5. **Para calibrar rise-to-wake**:
   - Desde app: Envía BLE comando calibration START
   - O: Toca botón largo para entrar calibration mode (manual, no implementado aquí)
   - Realiza 5 gestos de "levanta la muñeca"
   - App mostrará progreso (1/5 → 5/5)
   - Reloj guardará threshold en flash

---

## 🧪 Testing (sin hardware real)

Aunque no tengas el hardware aún, puedes compilar y verificar sin errores:

```bash
# Solo compilación (sin upload)
arduino-cli compile --fqbn Seeeduino:nrf52:Seeeduino_nRF52840_Sense couples_watch.ino
```

Esto valida:
- Sintaxis C++
- Librerías incluidas
- Tipos de datos
- Lógica de la máquina de estados

---

## 📊 Estados Principales

La máquina de estados incluye:

```
DEEP_SLEEP          → Bajo consumo (< 10µA)
WAKING_UP           → Transición de arranque
CLOCK_CONNECTED     → Reloj con BLE + colores blancos
CLOCK_DISCONNECTED  → Reloj sin BLE + colores azules
RADAR_MODE          → 1 LED señalando dirección de pareja
DISTANCE_MODE       → LEDs rellenados según distancia
HAPTIC_TX           → Flash confirmación + envío háptico
HAPTIC_RX           → Vibración + animación rosa
OTA_MODE            → Actualización firmware
ERROR_NO_GPS        → Error sin GPS (3 pulsos rojo)
LOW_BATTERY         → Overlay (1 pulso rojo cada 30s)
```

---

## 🔌 Pines GPIO (XIAO nRF52840)

```
D0  → RX (Serial)
D1  → TX (Serial)
D2  → (disponible)
D3  → (disponible)
D4  → I2C SDA (LIS3MDL)
D5  → I2C SCL (LIS3MDL)
D6  → (disponible)
D7  → SK6812 LED data (NeoPixel)
D8  → TTP223 button (entrada)
D9  → Motor vibración (PWM)
D10 → LED power MOSFET (HIGH = on)
```

---

## 🐛 Debugging

Descomenta líneas con `Serial.println()` en los archivos `.cpp` para más info:

**En `gesture.cpp` (detección de gestos):**
```cpp
// Serial.print("[GESTURE] Button: ");
// Serial.println(button_pressed ? "PRESSED" : "RELEASED");
```

**En `ble_handler.cpp` (BLE events):**
```cpp
// Serial.println("[BLE] Characteristic update");
```

**En `led_controller.cpp` (animaciones):**
```cpp
// Serial.print("[LED] Clock time: ");
// Serial.println(current_hour, current_minute, current_second);
```

---

## 🔄 Ciclo Principal (Loop)

El firmware ejecuta a ~100 Hz (10ms por iteración):

1. **Input**: Lee botón, BLE, sensores
2. **State Update**: Máquina de estados procesa transiciones
3. **Logic**: Ejecuta handlers de estado
4. **Output**: Escribe LEDs, motor, BLE

```
Loop timing (10ms):
  ├─ gesture_detector.update() — debounce + tap detection
  ├─ compass.update() — LIS3MDL heading
  ├─ ble_handler.update() — BLE events
  ├─ power_manager.update() — ADC battery
  ├─ led_controller.update() — animaciones
  ├─ haptic.update() — motor patterns
  └─ state_machine.update() — timers + transiciones
```

---

## 📱 Próximos pasos

1. **App Flutter**: Desarrollar app Android para GPS + BLE
2. **Backend Supabase**: Configurar docker-compose para Realtime
3. **OTA Updates**: Implementar Nordic DFU para actualizaciones OTA

---

## 📝 Licencia

MIT License — Libre para uso educativo y personal.

---

## 👨‍💻 Autor

Jessi Project v1.1 — Seeed XIAO nRF52840 Sense
