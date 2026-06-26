# 🎯 NEXUS HALO — Proyecto Completo

**Reloj inteligente parejas** con anillo LED de 12 posiciones, brújula magnética, vibración háptica y GPS en tiempo real.

---

## 📁 Estructura del Proyecto

```
Jessi/
├── Docs/
│   └── description.md              ← Especificación completa (v1.1)
│
├── nexus_halo/                        ← 🟢 IMPLEMENTADO 100%
│   ├── nexus_halo.ino           ← Main sketch (500+ líneas)
│   ├── config.h                    ← Constantes globales
│   ├── state_machine.h/.cpp        ← FSM (11 estados)
│   ├── gesture.h/.cpp              ← Detección gestos TTP223
│   ├── led_controller.h/.cpp       ← Control SK6812 (12 LEDs)
│   ├── compass.h/.cpp              ← LIS3MDL magnetómetro
│   ├── haptic.h/.cpp               ← Patrones vibración
│   ├── ble_handler.h/.cpp          ← BLE services
│   ├── power.h/.cpp                ← Deep sleep + power-down
│   └── README.md                   ← Guía compilación
│
├── app/                             ← 🔴 PRÓXIMO (Flutter Android)
│   ├── lib/
│   │   ├── main.dart
│   │   ├── services/
│   │   │   ├── ble_service.dart
│   │   │   ├── location_service.dart
│   │   │   ├── sync_service.dart
│   │   │   ├── bearing_calculator.dart
│   │   │   └── foreground_service.dart
│   │   ├── repositories/
│   │   │   └── partner_repository.dart
│   │   ├── screens/
│   │   ├── models/
│   │   └── widgets/
│   ├── pubspec.yaml
│   └── android/
│       └── AndroidManifest.xml     ← Permisos + Foreground Service
│
├── backend/                         ← 🔴 PRÓXIMO (Supabase)
│   ├── docker-compose.yml          ← Supabase self-hosted
│   ├── init-db.sql                 ← Tablas iniciales
│   └── README.md
│
└── PROYECTO.md                      ← Este archivo
```

---

## 🟢 FIRMWARE (COMPLETADO)

### Características

✅ **Máquina de estados (11 estados)**
- DEEP_SLEEP (<10µA)
- CLOCK_CONNECTED/DISCONNECTED (reloj 3 agujas)
- RADAR_MODE (1 LED → dirección pareja)
- DISTANCE_MODE (LEDs 1-11 según distancia)
- HAPTIC_TX/RX (vibración + confirmación)
- OTA_MODE (actualización firmware)
- ERROR_NO_GPS, LOW_BATTERY (superpuesto)

✅ **Detección de gestos**
- Tap simple, doble tap, press corto, press largo
- Debounce 20ms
- Double-tap window 400ms

✅ **Control LEDs (SK6812 - 12×)**
- Reloj: horas (discreto) + minutos (discreto) + segundos (interpolado suave)
- Radar: bearing relativo del usuario
- Distance: colores por rango (azul/verde/amarillo/naranja/rojo)
- Animaciones (flash, pulso, error)

✅ **Brújula (LIS3MDL)**
- I2C custom (D4, D5)
- Heading 0-360°
- Calibración hard-iron + soft-iron
- Filtro IIR suavizado

✅ **Vibración (Motor MOSFET)**
- 4 patrones: RX, TX, batería, error
- Playback no-bloqueante

✅ **BLE (ArduinoBLE)**
- Bearing, distance, haptic TX/RX
- Radar mode active notificación
- Battery %, config JSON

✅ **Power Management**
- Power-down LSM6DS3 (IMU) → -600µA
- Power-down PDM (micrófono) → -600µA
- ADC battery (0-100%)
- Deep sleep con BLE advertising mínimo

### Compilación Rápida

```bash
# Arduino IDE
1. File → Open → nexus_halo.ino
2. Tools → Board → Seeed XIAO nRF52840 Sense
3. Sketch → Verify (compile)
4. Sketch → Upload
5. Monitor (115200 baud)
```

### Librerías Requeridas

```
Adafruit_NeoPixel
Adafruit_LIS3MDL
Adafruit_LSM6DS3TRC
ArduinoBLE
Adafruit_Sensor
```

---

## 🔴 APP FLUTTER (PRÓXIMO)

### Requisitos
- **Android only** (Foreground Service para background persistent)
- GPS + BLE + Realtime Sync

### Flujo de Datos

```
GPS (background, polling dinámico)
  ↓ (3s / 60s / 3m / 5-10m según distancia)
Supabase → locations table
  ↓ (Realtime subscription)
App pareja recibe ubicación
  ↓ (bearing_calculator)
BLE → BEARING_CHAR + DISTANCE_CHAR reloj
  ↓
Reloj muestra RADAR o DISTANCE
```

### Paquetes Flutter

```yaml
flutter_reactive_ble: ^5.0.0       # BLE robusto
geolocator: ^11.0.0                # GPS background
supabase_flutter: ^2.0.0           # Sync
flutter_foreground_task: ^6.0.0    # Android Foreground Service
permission_handler: ^11.0.0        # Permisos
latlong2: ^0.9.0                   # Distancia + bearing
```

### Permisos Android

```xml
<uses-permission android:name="android.permission.FOREGROUND_SERVICE" />
<uses-permission android:name="android.permission.FOREGROUND_SERVICE_LOCATION" />
<uses-permission android:name="android.permission.FOREGROUND_SERVICE_CONNECTED_DEVICE" />
<uses-permission android:name="android.permission.ACCESS_FINE_LOCATION" />
<uses-permission android:name="android.permission.ACCESS_BACKGROUND_LOCATION" />
<uses-permission android:name="android.permission.BLUETOOTH_SCAN" />
<uses-permission android:name="android.permission.BLUETOOTH_CONNECT" />
```

### Dynamic Polling (crucial para batería)

| Modo | Distancia | Intervalo |
|---|---|---|
| PRECISION | <500m o RADAR activo | **3s** |
| NEAR | <10km | **60s** |
| FAR | 10-50km | **3m** |
| REMOTE | >50km | **5-10m** (jitter) |

**Impacto**: 95-99% menos tráfico que polling estático 3s.

---

## 🔴 BACKEND SUPABASE (PRÓXIMO)

### Infraestructura

```
PC siempre encendido (tuyo)
├── Docker Desktop
│   └── Supabase self-hosted
│       ├── PostgreSQL 15
│       ├── PostgREST
│       ├── Realtime (WebSockets)
│       └── Auth
└── Tailscale
    └── Acceso móviles desde tailnet
```

### Docker Compose

```yaml
version: '3.8'
services:
  postgres:
    image: supabase/postgres:15
    ...
  realtime:
    image: supabase/realtime:latest
    ...
  rest:
    image: postgrest/postgrest:latest
    ...
```

### Esquema de BD

```sql
CREATE TABLE locations (
  user_id TEXT PRIMARY KEY,
  latitude DOUBLE PRECISION,
  longitude DOUBLE PRECISION,
  accuracy FLOAT,
  updated_at TIMESTAMPTZ DEFAULT NOW()
);

CREATE TABLE haptic_events (
  id UUID PRIMARY KEY DEFAULT gen_random_uuid(),
  from_user TEXT,
  to_user TEXT,
  created_at TIMESTAMPTZ DEFAULT NOW(),
  consumed BOOLEAN DEFAULT FALSE
);

CREATE TABLE watch_config (
  user_id TEXT PRIMARY KEY,
  clock_timeout_s INT DEFAULT 5,
  brightness_percent INT DEFAULT 60,
  color_hours_connected TEXT DEFAULT 'FFFFDCB4',
  ...
);
```

### Realtime Subscriptions

```dart
// App B escucha eventos hápticos
supabase.from('haptic_events')
  .stream(primaryKey: ['id'])
  .eq('to_user', 'B')
  .eq('consumed', false)
  .listen((events) {
    if (events.isNotEmpty) {
      ble_service.sendHapticCommand();
      supabase.from('haptic_events')
        .update({'consumed': true})
        .eq('id', events.first['id']);
    }
  });
```

---

## 🔧 Configuración Inicial

### 1️⃣ Hardware (cuando llegue)

```
XIAO nRF52840 Sense
├── D7  → SK6812 data (NeoPixel)
├── D8  → TTP223 button
├── D9  → Motor vibración (PWM)
├── D10 → LED power MOSFET
├── D4  → LIS3MDL SDA (I2C)
└── D5  → LIS3MDL SCL (I2C)
```

### 2️⃣ Arduino IDE + Librerías

```
1. Tools → Board Manager → Seeed nRF52 Boards
2. Instalar Adafruit_NeoPixel, Adafruit_LIS3MDL, etc.
3. Tools → Board → Seeed XIAO nRF52840 Sense
4. Cargar firmware
```

### 3️⃣ Flutter App

```bash
flutter create jessi_app
cd jessi_app
flutter pub add flutter_reactive_ble geolocator supabase_flutter ...
```

### 4️⃣ Supabase Docker

```bash
docker-compose up -d
# Acceso: localhost:8000
# Tailscale: 100.x.x.x:8000 desde móviles
```

---

## 📊 Tablas de Referencia

### Estados y Gestos

| Estado | Tap | Doble tap | Press corto | Press largo |
|---|---|---|---|---|
| DEEP_SLEEP | WAKING_UP | — | — | — |
| CLOCK_CONN | Reset timer | HAPTIC_TX | RADAR | DEEP_SLEEP |
| CLOCK_DISC | Reset timer | (ignorado) | ERROR | DEEP_SLEEP |
| RADAR | Reset timer | DISTANCE | CLOCK | DEEP_SLEEP |
| DISTANCE | Reset timer | RADAR | CLOCK | DEEP_SLEEP |
| HAPTIC_RX | Cancelar | — | — | — |

### Colores (0xAARRGGBB)

**CLOCK_CONNECTED (blancos):**
- Horas: 0xFFFFDCB4 (cálido)
- Minutos: 0xFFFFF5F0 (neutro)
- Segundos: 0xFFC8DCFF (frío)

**CLOCK_DISCONNECTED (azules):**
- Horas: 0xFF001478 (oscuro)
- Minutos: 0xFF003CC8 (medio)
- Segundos: 0xFF2864FF (brillante)

**DISTANCE_MODE:**
- <15km: 0xFF0080FF (azul)
- 15-50km: 0xFF00CC44 (verde)
- 50-150km: 0xFFFFCC00 (amarillo)
- 150-350km: 0xFFFF6600 (naranja)
- >350km: 0xFFFF0000 (rojo)

### Patrones Háptica (ms)

**RX** (recibir toque):
```
200ms ON → 100ms OFF → 200ms ON → 100ms OFF → 400ms ON
```

**TX** (enviar toque):
```
100ms ON → 100ms OFF → 100ms ON (flash confirmación)
```

**Batería**:
```
100ms ON (cada 30 segundos)
```

---

## 🎯 Fases de Desarrollo

### Fase 1: Firmware ✅ COMPLETADO
- ✅ Máquina de estados
- ✅ Periféricos (GPIO, I2C, ADC, BLE)
- ✅ Power management
- ✅ Compilable sin hardware

### Fase 2: App Flutter ⏳ PRÓXIMO
- [ ] BLE GATT client
- [ ] GPS background
- [ ] Foreground Service (Android)
- [ ] Dynamic polling
- [ ] Previsualización del reloj

### Fase 3: Backend 🔴 FUTURO
- [ ] Docker Supabase
- [ ] Tablas PostgreSQL
- [ ] Realtime subscriptions
- [ ] Tailscale + auth

### Fase 4: Testing 🔴 FUTURO
- [ ] Test con hardware real
- [ ] OTA updates
- [ ] Calibración brújula
- [ ] Optimización batería

---

## 💡 Tips Importantes

1. **Power-down IMU/PDM es CRÍTICO**: Sin esto, deep sleep consume 1-3mA en lugar de <10µA.
   - Código en `nexus_halo/power.cpp` → `powerDownInternalSensors()`
   - Se ejecuta en `setup()` ANTES de inicializar otros periféricos

2. **I2C Custom**: LIS3MDL en D4/D5 requiere `Wire.setPins(D4, D5)` antes de `Wire.begin()`

3. **Máquina de estados**: Use `transitionTo()` para cambios. Los timers se resetean automáticamente según el estado.

4. **BLE Callbacks**: Las características se actualizan cuando el móvil escribe. Usa `callback_*` functions para reaccionar.

5. **Dynamic Polling**: Reduce GPS updates de 28.800/día (3s) a 144-288/día (>50km) → 99% menos tráfico.

---

## 📞 Soporte

- **Firmware**: Arduino IDE + Serial Monitor (115200 baud)
- **App**: Android Studio + Android emulator
- **Backend**: Docker logs (`docker logs supabase_postgres_1`)
- **Docs**: `Docs/description.md` (especificación completa v1.1)

---

## 📝 Licencia

MIT License — Libre para uso educativo y personal.

---

**Proyecto Jessi v1.1** | Seeed XIAO nRF52840 Sense | Flutter Android | Supabase
