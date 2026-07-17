# Couples Smartwatch — Especificación Completa del Proyecto
**Versión:** 1.2 — Pre-desarrollo  
**Rol:** Ingeniero de Software de Wearables  
**Estado:** Listo para implementación  
**Cambios v1.1:** Power-down IMU/mic en setup() | Android-only (Foreground Service) | GPS Dynamic Polling  
**Cambios v1.2:** Rise-to-wake via IMU LSM6DS3 (low-power wake-on-motion) — dos fuentes de wake en DEEP_SLEEP

---

## 1. HARDWARE (REFERENCIA ESTRICTA — NO MODIFICAR)

| Componente | Chip / Módulo | Pin | Notas |
|---|---|---|---|
| Microcontrolador | Seeed Studio XIAO nRF52840 **Sense** | — | Incluye IMU LSM6DS3 integrado. Soporta BLE 5.4 y OTA. |
| Anillo LEDs | 12× SK6812-MINI-E (RGB) | `D7` (datos) | LED 1 = posición 12h. Orden antihorario (mapeado horario en software). |
| Corte energía LEDs | MOSFET | `D10` | HIGH = LEDs con corriente. LOW = corte total (deep sleep). |
| Botón táctil | TTP223 | `D8` | Solo HIGH/LOW. Sin detección nativa de gestos. Toggle mode OFF. |
| Motor vibración | MOSFET | `D9` | HIGH = vibra. Control PWM opcional para intensidad. |
| Magnetómetro | LIS3MDL | `D4` SDA / `D5` SCL | I2C custom (no los pines I2C por defecto del XIAO). |

**Nota crítica — Fuentes de wake del DEEP_SLEEP:** El reloj tiene **dos mecanismos de despertar** independientes:
- **D8 (TTP223):** Tap del usuario → siempre disponible
- **IMU LSM6DS3 (wake-on-motion):** Levantar la muñeca → rise-to-wake

El IMU **no** entra en Power-Down total durante DEEP_SLEEP; se configura en modo Low-Power con umbral de aceleración, consumiendo ~25µA adicionales a cambio del rise-to-wake. Ver sección 6.6 para el trade-off de batería completo.

---

## 2. ARQUITECTURA DE ESTADOS (MÁQUINA DE ESTADOS COMPLETA)

### 2.1 Lista de Estados

```
DEEP_SLEEP       → Estado base de bajo consumo
WAKING_UP        → Transitorio de arranque (<200ms)
CLOCK_CONNECTED  → Reloj con conexión BLE activa
CLOCK_DISCONNECTED → Reloj sin conexión BLE
RADAR_MODE       → Indicador de dirección hacia la pareja
DISTANCE_MODE    → Indicador de distancia hacia la pareja
HAPTIC_TX        → Enviando toque al móvil (transitorio)
HAPTIC_RX        → Recibiendo vibración de la pareja
OTA_MODE         → Recibiendo actualización de firmware
LOW_BATTERY      → Superpuesto a cualquier estado
ERROR_NO_GPS     → Error transitorio: sin fix GPS
```

### 2.2 Descripción Detallada de Cada Estado

#### `DEEP_SLEEP`
- `D10` = LOW (LEDs sin corriente)
- Motor D9 = OFF
- BLE: advertising mínimo (solo para reconexión del móvil)
- CPU: modo sleep máximo del nRF52840
- **Dos eventos de salida posibles:**
  - Interrupción rising edge en `D8` (tap del usuario) → `WAKING_UP`
  - Interrupción IMU LSM6DS3 wake-on-motion (levantar muñeca) → `WAKING_UP`
- Consumo objetivo: ~30–35µA (vs <10µA sin rise-to-wake — trade-off aceptado, ver sección 6.6)

#### `WAKING_UP` (transitorio, <200ms)
- `D10` = HIGH (da corriente al anillo)
- Comprueba estado BLE:
  - Conectado → entra en `CLOCK_CONNECTED`
  - Desconectado → entra en `CLOCK_DISCONNECTED`
- No muestra LEDs durante este estado

#### `CLOCK_CONNECTED`
- Muestra la hora mediante 3 "agujas" LED sobre 12 posiciones
- LEDs en escala de **blancos** (configurable por app)
- Prioridad de agujas si coinciden en el mismo LED: **Segundos > Minutos > Horas**
- Aguja de segundos: interpolación suave entre LEDs adyacentes (ej: s=5 → LED1 al 80%, LED2 al 20%)
- Aguja de minutos: posición discreta (1 LED encendido)
- Aguja de horas: posición discreta (1 LED encendido)
- Sin pulsaciones. Si la escala de brillo es logarítmica (para que el ojo la perciba lineal), configurable.
- Timer configurable (default 5s) → `DEEP_SLEEP`

#### `CLOCK_DISCONNECTED`
- Idéntico a `CLOCK_CONNECTED` en comportamiento
- LEDs en escala de **azules fríos** (configurable por app)
- Timer configurable (default 5s) → `DEEP_SLEEP`

#### `RADAR_MODE`
- Solo disponible si BLE conectado
- Enciende **1 LED** (o interpolación suave entre 2 LEDs adyacentes) apuntando físicamente hacia la pareja
- Cálculo: `LED_index = round((bearing_relativo / 360) * 12) % 12`
  - `bearing_relativo = bearing_pareja - heading_brujula`
- Color: blanco cálido / ámbar (configurable)
- Brillo configurable
- El bearing llega por BLE desde el móvil
- Timer configurable (default 5s sin interacción) → `CLOCK_CONNECTED`

#### `DISTANCE_MODE`
- Solo disponible si BLE conectado
- Rellena LEDs en sentido **horario desde LED 1** (posición 12h) según distancia
- **Escala de distancia y colores:**

```
LEDs 1–4   (0   – ~15km)  → Azul      (#0080FF)   "cerca, zona cotidiana"
LEDs 5–7   (15  – ~50km)  → Verde     (#00CC44)   "misma provincia"
LEDs 8–9   (50  – ~150km) → Amarillo  (#FFCC00)   "viaje de día"
LEDs 10–11 (150 – ~350km) → Naranja   (#FF6600)   "muy lejos"
LED  12    (350 – ~500km) → Rojo      (#FF0000)   "al límite"
```

- Fórmula de interpolación dentro de cada rango:
  ```
  leds_encendidos = floor(distancia_normalizada * 11) + 1   // de 1 a 11
  led_parcial = (distancia_normalizada * 11) % 1            // fracción del siguiente LED
  ```
  El último LED activo puede estar a brillo parcial para mayor suavidad.

- Si distancia > 500km: 11 LEDs en rojo, sin parpadeo (escala logarítmica si activado)
- Timer configurable (default 5s sin interacción) → `CLOCK_CONNECTED`

#### `HAPTIC_TX` (transitorio ~500ms)
- El usuario ha hecho doble tap
- Envía comando `HAPTIC_SEND` por BLE al móvil
- LEDs: flash blanco rápido (2 pulsos) como confirmación
- Vuelve automáticamente al estado anterior

#### `HAPTIC_RX`
- Llega comando BLE del móvil: `HAPTIC_RECEIVE`
- **Interrumpe cualquier estado activo** (excepto `OTA_MODE` y `DEEP_SLEEP`)
- Motor D9: patrón de vibración (ver sección 5)
- LEDs: pulsan en rosa/rojo suave sincronizados con motor
- Tap simple → cancela y vuelve al estado anterior
- Sin input del usuario: finaliza con el patrón y vuelve solo

#### `OTA_MODE`
- Firmware update en progreso
- **Bloqueado**: no acepta ningún gesto ni comando BLE excepto datos OTA
- LEDs: indicador de progreso, relleno horario desde LED 1
  - Color: azul girando mientras espera, luego azul fijo llenando según % completado
  - `leds_encendidos = round(porcentaje / 100 * 12)`
  - Al llegar a 100%: flash verde completo → reinicio
- Si falla: flash rojo → vuelve al estado previo

#### `ERROR_NO_GPS` (transitorio ~2s)
- El usuario intentó entrar en RADAR o DISTANCE pero el móvil no tiene fix GPS
- LEDs: 3 pulsos rojo rápido (toda la corona)
- Vuelve automáticamente a `CLOCK_CONNECTED`

#### `LOW_BATTERY` (superpuesto)
- Se activa cuando batería < umbral configurable (default 15%)
- No interrumpe el estado actual, se superpone visualmente
- Cada 30 segundos: 1 pulso rojo lento en LED 12 (o el LED más cercano a las 6h)
- Si batería < 5%: fuerza `DEEP_SLEEP` inmediatamente

---

## 3. GESTOS (TTP223 — DETECCIÓN POR SOFTWARE)

### 3.1 Definición temporal de gestos

```
Tap simple:    HIGH durante 50ms–500ms, seguido de LOW
Doble tap:     Dos taps simples con <400ms entre el final del 1º y el inicio del 2º
Press corto:   HIGH sostenido 1500ms–2999ms
Press largo:   HIGH sostenido ≥3000ms
```

### 3.2 Tabla de acciones por estado y gesto

| Estado actual | Tap simple | Doble tap | Press corto (1.5s) | Press largo (3s) |
|---|---|---|---|---|
| `DEEP_SLEEP` | Wake up → `WAKING_UP` | — | — | — |
| `CLOCK_CONNECTED` | Reset timer de auto-sleep | `HAPTIC_TX` | Toggle → `RADAR_MODE` | `DEEP_SLEEP` forzado |
| `CLOCK_DISCONNECTED` | Reset timer de auto-sleep | Error (sin conexión, ignorar) | Toggle → error LED rojo | `DEEP_SLEEP` forzado |
| `RADAR_MODE` | Reset timer | Toggle → `DISTANCE_MODE` | Toggle → `CLOCK_CONNECTED` | `DEEP_SLEEP` forzado |
| `DISTANCE_MODE` | Reset timer | Toggle → `RADAR_MODE` | Toggle → `CLOCK_CONNECTED` | `DEEP_SLEEP` forzado |
| `HAPTIC_RX` | Cancela vibración → estado anterior | — | — | — |
| `OTA_MODE` | Ignorado | Ignorado | Ignorado | Ignorado |

**Notas:**
- En `CLOCK_DISCONNECTED`, el press corto intenta ir a RADAR pero no hay conexión → `ERROR_NO_GPS` (mismo flash rojo, mensaje de "sin conexión")
- El doble tap en `CLOCK_DISCONNECTED` se ignora silenciosamente (sin LEDs de error, para no confundir)

---

## 4. LÓGICA DEL RELOJ (CLOCK_MODE)

### 4.1 Mapeo hora → LED

```
12 LEDs representan 12 horas / 60 minutos / 60 segundos

Horas:    LED = floor(hora_12h / 12 * 12) = hora_12h (directo)
Minutos:  LED = floor(minuto / 60 * 12)
Segundos: interpolación continua entre dos LEDs adyacentes
```

### 4.2 Interpolación suave de segundos

```cpp
float pos_segundos = (segundo + milisegundo/1000.0) / 60.0 * 12.0;
int led_principal = floor(pos_segundos) % 12;
int led_siguiente = (led_principal + 1) % 12;
float fraccion = pos_segundos - floor(pos_segundos);

// Brillo logarítmico para percepción lineal del ojo
brillo_principal = 255 * pow(1.0 - fraccion, 2.2);
brillo_siguiente = 255 * pow(fraccion, 2.2);
```

### 4.3 Colores (configurables desde app)

**CLOCK_CONNECTED (defaults):**
```
Horas:    Blanco cálido   R:255 G:220 B:180
Minutos:  Blanco neutro   R:255 G:255 B:240
Segundos: Blanco frío     R:200 G:220 B:255
Brillo global: 60% (configurable 10%–100%)
```

**CLOCK_DISCONNECTED (defaults):**
```
Horas:    Azul oscuro     R:0   G:20  B:120
Minutos:  Azul medio      R:0   G:60  B:200
Segundos: Azul brillante  R:40  G:100 B:255
Brillo global: 40% (configurable 10%–100%)
```

### 4.4 Resolución de colisiones entre agujas

Cuando dos o más agujas coinciden en el mismo LED:
- **Regla de prioridad:** Segundos > Minutos > Horas
- El LED muestra únicamente el color de mayor prioridad
- El usuario infiere que las otras agujas están "debajo"
- Excepción: si el brillo del LED de segundos es < 20% por la interpolación, se muestra el color de minutos/horas para que no quede negro

---

## 5. PATRONES DE VIBRACIÓN

### 5.1 Patrón HAPTIC_RX (recibir toque de la pareja)
```
Vibra 200ms → pausa 100ms → vibra 200ms → pausa 100ms → vibra 400ms → FIN
```

### 5.2 Patrón de confirmación HAPTIC_TX (enviado correctamente)
```
(Solo LEDs, motor no) → 2× flash blanco 100ms con 100ms pausa
```

### 5.3 Patrón de batería crítica
```
1× vibra 100ms cada 30 segundos (muy suave, informativo)
```

### 5.4 Patrón OTA completo
```
1× vibra larga 500ms al finalizar la actualización
```

---

## 6. FIRMWARE — ARQUITECTURA (Arduino/C++ para nRF52840)

### 6.1 Librerías necesarias

```
Adafruit_NeoPixel o FastLED          → Control SK6812
Adafruit_LIS3MDL                     → Magnetómetro I2C custom
ArduinoBLE o Bluefruit SDK (Adafruit)→ BLE + OTA
Adafruit_LSM6DS3TRC                  → IMU — OBLIGATORIO inicializar para apagar
RTClib o RTC nRF52840 interno        → Reloj de tiempo real
```

> ⚠️ **CRÍTICO — Power-down de periféricos internos del Sense:**
> El XIAO nRF52840 Sense incluye el IMU (LSM6DS3) y un micrófono PDM integrados.
> Aunque no se usen, **siguen drenando corriente si no se apagan explícitamente**.
> Sin esta inicialización, el target de <10µA en DEEP_SLEEP es **inalcanzable**.
> Ambos dispositivos deben inicializarse y mandarse a Power Down en `setup()`.

### 6.2 Estructura de archivos firmware

```
/firmware
├── nexus_halo.ino          → Setup, loop, máquina de estados principal
├── ble_handler.h/.cpp         → Gestión BLE, servicios, características, OTA
├── led_controller.h/.cpp      → Control SK6812, animaciones, estados LED
├── compass.h/.cpp             → LIS3MDL I2C custom, calibración, heading
├── gesture.h/.cpp             → Detección de gestos TTP223 por software
├── haptic.h/.cpp              → Patrones de vibración motor D9
├── state_machine.h/.cpp       → FSM, transiciones, timers
├── config.h                   → Constantes, pines, parámetros configurables
└── power.h/.cpp               → Deep sleep, wake, gestión batería
```

### 6.3 Servicios BLE

```
Service UUID: "couples-watch-service" (custom 128-bit)

Características:
  BEARING_CHAR         → WRITE    (móvil escribe el rumbo, 0–360, float 4 bytes)
  DISTANCE_CHAR        → WRITE    (móvil escribe distancia en metros, uint32)
  HAPTIC_TX_CHAR       → NOTIFY   (reloj notifica al móvil que el usuario tocó)
  HAPTIC_RX_CHAR       → WRITE    (móvil escribe para activar vibración)
  RADAR_ACTIVE_CHAR    → NOTIFY   (reloj notifica 0x01 al entrar en RADAR_MODE, 0x00 al salir)
  CONFIG_CHAR          → WRITE+READ (app escribe config JSON comprimido)
  BATTERY_CHAR         → NOTIFY   (reloj notifica % batería, uint8)
  OTA_CHAR             → estándar Nordic DFU sobre BLE
```

### 6.4 Esquema de la máquina de estados principal (pseudocódigo)

```cpp
void loop() {
  gesture_t gesto = gesture_handler.update();
  ble_event_t ble_event = ble_handler.update();
  
  switch (estado_actual) {
    case CLOCK_CONNECTED:
      led_controller.show_clock(connected=true);
      handle_gesture_clock(gesto);
      if (ble_event == HAPTIC_RECEIVE) transicion(HAPTIC_RX);
      if (!ble.connected()) transicion(CLOCK_DISCONNECTED);
      if (timer_expired()) transicion(DEEP_SLEEP);
      break;
      
    // ... resto de estados
  }
  
  if (bateria_critica()) superponer_LOW_BATTERY();
}
```

### 6.5 Deep Sleep / Wake

Dos fuentes de interrupción configuradas antes de entrar en sleep. Ambas apuntan al mismo ISR o flags distintos según se quiera diferenciar el origen del wake (útil para logging o comportamiento diferenciado en el futuro).

```cpp
void go_to_deep_sleep() {
  digitalWrite(D10, LOW);                    // Corta LEDs
  digitalWrite(D9, LOW);                     // Para motor

  // Fuente 1: tap del usuario (TTP223)
  attachInterrupt(D8, wake_from_tap_isr, RISING);

  // Fuente 2: rise-to-wake (LSM6DS3 wake-on-motion)
  // El IMU ya está configurado en low-power con umbral en setup()
  // Su pin INT1 está conectado internamente; aquí solo habilitamos la escucha
  attachInterrupt(IMU_INT1_PIN, wake_from_motion_isr, RISING);

  ble.advertise_minimal();                   // Advertising de bajo consumo
  sd_app_evt_wait();                         // WFE — CPU duerme, SoftDevice activo
}

void wake_from_tap_isr()    { wake_source = WAKE_TAP;    }
void wake_from_motion_isr() { wake_source = WAKE_MOTION; }
```

> **Nota:** `IMU_INT1_PIN` es el pin GPIO del nRF52840 al que está conectado físicamente el pin INT1 del LSM6DS3 en la PCB del XIAO Sense. Verificar en el esquemático de Seeed (pin P0.11 según datasheet del Sense). Definir como constante en `config.h`.

### 6.6 Setup() obligatorio — Configuración de periféricos internos Sense

#### IMU LSM6DS3 — Low-Power + Wake-on-Motion (NO Power-Down)

El IMU **no se apaga completamente**. Se configura en modo Low-Power con el acelerómetro activo a baja frecuencia y un umbral de detección de movimiento que genera una interrupción en INT1 al levantar la muñeca.

```cpp
#include <Adafruit_LSM6DS3TRC.h>
#include <PDM.h>

Adafruit_LSM6DS3TRC imu;
#define IMU_INT1_PIN  PIN_LSM6DS3TR_C_INT1  // Verificar en config de Seeed Sense
                                             // Típicamente P0.11 según esquemático

void setup() {
  Wire.setPins(D4, D5);  // I2C custom — mismo bus que LIS3MDL

  if (imu.begin_I2C()) {

    // --- Giroscopio: apagado total (no se necesita para wake-on-motion) ---
    imu.setGyroDataRate(LSM6DS_RATE_SHUTDOWN);

    // --- Acelerómetro: Low-Power a 26Hz (mínimo que soporta wake-on-motion) ---
    imu.setAccelDataRate(LSM6DS_RATE_26_HZ);
    imu.setAccelRange(LSM6DS_ACCEL_RANGE_2_G);

    // --- Configurar Wake-on-Motion vía registros directos ---
    // WAKE_UP_THS: umbral de detección (~156mg por bit en ±2g)
    // Valor 0x02 ≈ 312mg — suficiente para detectar levantar muñeca sin falsos positivos
    imu.writeRegister(LSM6DS_WAKE_UP_THS, 0x02);

    // WAKE_UP_DUR: duración mínima del movimiento para disparar (1 muestra a 26Hz ≈ 38ms)
    imu.writeRegister(LSM6DS_WAKE_UP_DUR, 0x00);

    // MD1_CFG: enrutar wake-up interrupt a INT1
    imu.writeRegister(LSM6DS_MD1_CFG, 0x20);

    // Configurar INT1 del nRF52840 para escuchar el pin físico del IMU
    pinMode(IMU_INT1_PIN, INPUT);
    attachInterrupt(IMU_INT1_PIN, wake_from_motion_isr, RISING);
  }

  // --- Micrófono PDM: Power-Down total (no se usa en ningún estado) ---
  PDM.begin(1, 16000);
  PDM.end();

  // --- Resto del setup normal ---
  pinMode(D10, OUTPUT);  digitalWrite(D10, LOW);
  pinMode(D9,  OUTPUT);  digitalWrite(D9,  LOW);
  pinMode(D8,  INPUT);
}
```

#### Trade-off de consumo en DEEP_SLEEP

| Componente | Sin rise-to-wake (v1.1) | **Con rise-to-wake (v1.2)** |
|---|---|---|
| IMU LSM6DS3 (accel low-power 26Hz) | <1µA (shutdown) | **~25µA** |
| Giroscopio | <1µA | <1µA (shutdown) |
| Micrófono PDM | <1µA | <1µA (off) |
| nRF52840 + BLE adv. | ~4µA | ~4µA |
| **Total estimado** | **~8µA** | **~30–35µA** |

#### Impacto en batería (140mAh, uso típico 15 min activo/día)

| Config | Consumo sleep | Duración estimada |
|---|---|---|
| Sin rise-to-wake | ~8µA | ~38 días |
| **Con rise-to-wake** | **~30µA** | **~32 días** |

**Diferencia: ~6 días.** Completamente aceptable a cambio de la funcionalidad.

#### Umbral de sensibilidad — ajuste fino

El valor `0x02` en `WAKE_UP_THS` (~312mg) es el punto de partida recomendado. En pruebas reales puede necesitar ajuste:
- Si despierta con movimientos involuntarios → aumentar (0x03, 0x04)
- Si no detecta el gesto de levantar muñeca → reducir (0x01)
- Este valor puede hacerse configurable desde la app vía `CONFIG_CHAR`

---

## 7. APP MÓVIL — ARQUITECTURA

### 7.1 Framework elegido: Flutter — Android Only

**Plataforma objetivo: Android exclusivamente.**

**Justificación técnica:** La app utiliza un **Android Foreground Service** con notificación persistente (silenciosa) para garantizar que el proceso no sea destruido por el Doze Mode ni por la gestión agresiva de memoria de Android. Esto permite mantener simultáneamente la conexión BLE activa y la lectura GPS en background de forma fiable e indefinida, que es el requisito principal del sistema.

Eliminar el soporte iOS simplifica radicalmente la arquitectura: no hay restricciones de `Info.plist`, no hay `Background Modes: Location updates`, no hay limitaciones de 30 segundos de ejecución en background de iOS, y no hay que gestionar la renovación de permisos de localización `Always` bajo las políticas de App Store.

### 7.2 Estructura de la app

```
/app
├── lib/
│   ├── main.dart
│   ├── services/
│   │   ├── ble_service.dart               → Conexión y comunicación BLE
│   │   ├── location_service.dart          → GPS en background (dynamic polling)
│   │   ├── sync_service.dart              → Sincronización con Supabase
│   │   ├── bearing_calculator.dart        → Cálculo de rumbo y distancia
│   │   └── foreground_service.dart        → Android Foreground Service controller
│   ├── repositories/
│   │   └── partner_repository.dart        → Lectura/escritura de ubicación pareja
│   ├── screens/
│   │   ├── home_screen.dart               → Estado de conexión, mapa
│   │   ├── settings_screen.dart           → Config del reloj (colores, timers...)
│   │   └── pairing_screen.dart            → Vinculación inicial
│   ├── models/
│   │   ├── location_model.dart
│   │   └── config_model.dart
│   └── widgets/
│       └── watch_preview_widget.dart      → Previsualización del reloj en app
└── android/
    └── app/src/main/
        ├── AndroidManifest.xml            → Permisos BLE, GPS, FOREGROUND_SERVICE
        └── res/drawable/
            └── ic_notification_silent.png → Icono notificación persistente
```

### 7.3 Paquetes Flutter necesarios (Android)

```yaml
dependencies:
  flutter_reactive_ble: ^5.0.0           # BLE robusto en background
  geolocator: ^11.0.0                    # GPS en background (Android)
  supabase_flutter: ^2.0.0              # Cliente Supabase self-hosted
  flutter_foreground_task: ^6.0.0       # Android Foreground Service nativo
  permission_handler: ^11.0.0           # Permisos en runtime (ACCESS_FINE_LOCATION, BLUETOOTH_SCAN, etc.)
  latlong2: ^0.9.0                      # Cálculo de distancia y bearing
```

**Permisos requeridos en `AndroidManifest.xml`:**
```xml
<uses-permission android:name="android.permission.FOREGROUND_SERVICE" />
<uses-permission android:name="android.permission.FOREGROUND_SERVICE_LOCATION" />
<uses-permission android:name="android.permission.FOREGROUND_SERVICE_CONNECTED_DEVICE" />
<uses-permission android:name="android.permission.ACCESS_FINE_LOCATION" />
<uses-permission android:name="android.permission.ACCESS_BACKGROUND_LOCATION" />
<uses-permission android:name="android.permission.BLUETOOTH_SCAN" />
<uses-permission android:name="android.permission.BLUETOOTH_CONNECT" />
```

**Funcionamiento del Foreground Service:**
- Al arrancar la app, se inicia un `ForegroundTask` de tipo `location` y `connectedDevice`
- Muestra una notificación persistente **silenciosa** (sin sonido, sin vibración, sin heads-up) con el texto "NEXUS HALO activo" y un icono minimalista
- El Foreground Service garantiza que Android no destruya el proceso en Doze Mode, incluso con la pantalla apagada durante horas
- Dentro del servicio corren: el loop de GPS, el cliente BLE, y el cliente Supabase Realtime

### 7.4 Flujo de datos principal — Dynamic GPS Polling

El intervalo de actualización de coordenadas a Supabase **no es estático**. Escala dinámicamente según la distancia entre ambos usuarios, para optimizar el consumo de batería del móvil, la carga de CPU del Foreground Service y el tráfico por Tailscale.

#### Tabla de intervalos dinámicos

| Modo | Condición de activación | Intervalo GPS → Supabase | Justificación |
|---|---|---|---|
| `PRECISION` | Distancia < 500m **O** reloj en `RADAR_MODE` activo | **3 segundos** | Navegación cercana, el bearing cambia rápido |
| `NEAR` | Distancia < 10km | **60 segundos** | Zona cotidiana, cambios lentos |
| `FAR` | Distancia 10km – 50km | **3 minutos** | Misma ciudad o área metropolitana |
| `REMOTE` | Distancia > 50km | **5–10 minutos** | Largos desplazamientos, baja necesidad de precisión |

#### Lógica de transición entre modos (pseudocódigo Dart)

```dart
// En location_service.dart — dentro del Foreground Service
class DynamicPollingManager {
  GpsPollingMode _currentMode = GpsPollingMode.REMOTE;
  bool _radarModeActive = false; // recibido por BLE desde el reloj

  Duration get currentInterval {
    if (_radarModeActive || _lastDistance < 0.5) return Duration(seconds: 3);
    if (_lastDistance < 10)                      return Duration(seconds: 60);
    if (_lastDistance < 50)                      return Duration(minutes: 3);
    return Duration(minutes: randomBetween(5, 10)); // jitter para evitar colisiones
  }

  void onNewPartnerLocation(LatLng myPos, LatLng partnerPos) {
    _lastDistance = calculateDistance(myPos, partnerPos); // km
    _scheduleNextUpdate();
  }

  void onRadarModeChanged(bool active) {
    _radarModeActive = active;
    if (active) _forceImmediateUpdate(); // actualización inmediata al entrar en RADAR
  }

  void _scheduleNextUpdate() {
    _timer?.cancel();
    _timer = Timer(currentInterval, _doGpsUpdate);
  }

  Future<void> _doGpsUpdate() async {
    final pos = await Geolocator.getCurrentPosition();
    await supabase.from('locations').upsert({
      'user_id': myUserId,
      'latitude': pos.latitude,
      'longitude': pos.longitude,
      'accuracy': pos.accuracy,
      'polling_mode': currentMode.name, // para debugging en Supabase
    });
    _scheduleNextUpdate(); // reprograma el siguiente ciclo
  }
}
```

#### Señal BLE de RADAR_MODE activo → trigger de precisión

Cuando el reloj entra en `RADAR_MODE` (press corto desde CLOCK), el firmware notifica al móvil escribiendo en una característica BLE `RADAR_ACTIVE_CHAR` (valor 0x01). La app lee esto y fuerza inmediatamente el modo `PRECISION` (3 segundos), independientemente de la distancia. Al salir de RADAR, envía 0x00 y la app vuelve al intervalo correspondiente a la distancia actual.

```
Flujo completo:
GPS (background — intervalo dinámico)
    ↓
Supabase Realtime → tabla "locations"
    ↓ (suscripción en tiempo real)
App pareja recibe nueva ubicación
    ↓
bearing_calculator.dart calcula rumbo + distancia → actualiza DynamicPollingManager
    ↓
BLE → escribe en BEARING_CHAR y DISTANCE_CHAR del reloj
    ↓
El reloj actualiza RADAR_MODE o DISTANCE_MODE
```

**Impacto estimado en batería del móvil:**

| Escenario típico | Polling estático 3s | Dynamic Polling |
|---|---|---|
| Noche en casa (misma cama, <10m) | 28.800 writes/día | ~1.440 writes/día |
| Día normal (misma ciudad, ~5km) | 28.800 writes/día | ~1.440 writes/día |
| Ella en otra ciudad (>50km) | 28.800 writes/día | ~144–288 writes/día |
| Reducción de tráfico Tailscale | — | **~95–99%** |

### 7.5 Flujo háptico

```
Usuario toca reloj A (doble tap)
    ↓
Reloj A notifica por BLE → HAPTIC_TX_CHAR
    ↓
App A recibe notificación BLE
    ↓
App A escribe en Supabase: haptic_events { from: "A", to: "B", timestamp }
    ↓
App B recibe el evento vía Realtime subscription
    ↓
App B escribe HAPTIC_RX_CHAR en reloj B por BLE
    ↓
Reloj B vibra con patrón definido
```

---

## 8. BACKEND — SUPABASE SELF-HOSTED CON TAILSCALE

### 8.1 Infraestructura

```
PC siempre encendido (tuyo)
├── Docker Desktop / Docker Engine
│   ├── Supabase self-hosted (docker-compose oficial)
│   │   ├── PostgreSQL 15
│   │   ├── PostgREST (API REST automática)
│   │   ├── Supabase Realtime (WebSockets para sync en tiempo real)
│   │   └── Supabase Auth (autenticación usuarios)
│   └── Expuesto en localhost:8000 (o puerto que elijas)
└── Tailscale
    └── Acceso desde los móviles vía IP de Tailscale
        └── URL: http://100.x.x.x:8000 (IP de Tailscale de tu PC)
```

### 8.2 Esquema de base de datos

```sql
-- Ubicaciones en tiempo real
CREATE TABLE locations (
  user_id     TEXT PRIMARY KEY,  -- "A" o "B"
  latitude    DOUBLE PRECISION,
  longitude   DOUBLE PRECISION,
  accuracy    FLOAT,
  updated_at  TIMESTAMPTZ DEFAULT NOW()
);

-- Eventos hápticos (se borran tras consumirse)
CREATE TABLE haptic_events (
  id          UUID PRIMARY KEY DEFAULT gen_random_uuid(),
  from_user   TEXT,             -- "A" o "B"
  to_user     TEXT,             -- "B" o "A"
  created_at  TIMESTAMPTZ DEFAULT NOW(),
  consumed    BOOLEAN DEFAULT FALSE
);

-- Configuración del reloj (sincronizada desde app)
CREATE TABLE watch_config (
  user_id                 TEXT PRIMARY KEY,
  clock_timeout_s         INT DEFAULT 5,
  sleep_timeout_s         INT DEFAULT 5,
  color_hours_connected   TEXT DEFAULT 'FFFFDCB4',
  color_minutes_connected TEXT DEFAULT 'FFFFF5F0',
  color_seconds_connected TEXT DEFAULT 'FFC8DCFF',
  color_hours_disc        TEXT DEFAULT 'FF001478',
  color_minutes_disc      TEXT DEFAULT 'FF003CC8',
  color_seconds_disc      TEXT DEFAULT 'FF2864FF',
  brightness_percent      INT DEFAULT 60,
  low_battery_threshold   INT DEFAULT 15,
  haptic_pattern          TEXT DEFAULT 'default',
  updated_at              TIMESTAMPTZ DEFAULT NOW()
);
```

### 8.3 Realtime subscriptions

```dart
// En App B: escuchando eventos hápticos dirigidos a B
supabase.from('haptic_events')
  .stream(primaryKey: ['id'])
  .eq('to_user', 'B')
  .eq('consumed', false)
  .listen((events) {
    if (events.isNotEmpty) {
      ble_service.sendHapticCommand();
      // Marcar como consumido
      supabase.from('haptic_events')
        .update({'consumed': true})
        .eq('id', events.first['id']);
    }
  });
```

---

## 9. CÁLCULO DE RUMBO Y DISTANCIA

### 9.1 Bearing (rumbo) — Fórmula Haversine + atan2

```dart
double calculateBearing(LatLng from, LatLng to) {
  double lat1 = radians(from.latitude);
  double lat2 = radians(to.latitude);
  double dLon = radians(to.longitude - from.longitude);

  double y = sin(dLon) * cos(lat2);
  double x = cos(lat1) * sin(lat2) - sin(lat1) * cos(lat2) * cos(dLon);

  return (degrees(atan2(y, x)) + 360) % 360; // 0–360°, Norte = 0
}
```

### 9.2 Distancia — Haversine

```dart
double calculateDistance(LatLng from, LatLng to) {
  // Usando el paquete latlong2: Distance().as(LengthUnit.Meter, from, to)
  return Distance().as(LengthUnit.Kilometer, from, to);
}
```

### 9.3 LED índice desde bearing relativo

```cpp
// En el firmware
float heading = compass.getHeading();    // 0–360° Norte magnético
float bearing = ble.getBearing();        // 0–360° enviado desde app

float relativo = fmod((bearing - heading + 360.0), 360.0);
int led_index = round(relativo / 360.0 * 12.0) % 12;
// LED 0 = posición 12h = Norte del reloj
```

---

## 10. PARÁMETROS CONFIGURABLES DESDE LA APP

Todos estos valores se sincronizan desde la app al reloj por BLE (via CONFIG_CHAR) y también se persisten en Supabase:

| Parámetro | Default | Descripción |
|---|---|---|
| `clock_timeout_s` | 5s | Tiempo en RADAR/DISTANCE antes de volver a CLOCK |
| `sleep_timeout_s` | 5s | Tiempo en CLOCK antes de DEEP_SLEEP |
| `brightness_percent` | 60% | Brillo global LEDs (escala logarítmica) |
| `color_hours_connected` | Blanco cálido | Color aguja horas (conectado) |
| `color_minutes_connected` | Blanco neutro | Color aguja minutos (conectado) |
| `color_seconds_connected` | Blanco frío | Color aguja segundos (conectado) |
| `color_hours_disc` | Azul oscuro | Color aguja horas (desconectado) |
| `color_minutes_disc` | Azul medio | Color aguja minutos (desconectado) |
| `color_seconds_disc` | Azul brillante | Color aguja segundos (desconectado) |
| `low_battery_threshold` | 15% | % batería para activar LOW_BATTERY |
| `haptic_pattern` | `default` | Patrón de vibración al recibir toque |
| `gps_interval_precision_s` | 3s | Intervalo GPS en modo PRECISION (<500m o RADAR activo) |
| `gps_interval_near_s` | 60s | Intervalo GPS en modo NEAR (<10km) |
| `gps_interval_far_s` | 180s | Intervalo GPS en modo FAR (10–50km) |
| `gps_interval_remote_s` | 300–600s | Intervalo GPS en modo REMOTE (>50km), con jitter aleatorio |
| `logarithmic_brightness` | true | Curva gamma para percepción lineal del ojo |

---

## 11. OTA (OVER THE AIR UPDATE)

- Protocolo: Nordic DFU sobre BLE (estándar nRF52840)
- Herramienta de actualización: nRF Connect for Mobile (app oficial de Nordic)
- El reloj entra en OTA_MODE al recibir el paquete DFU inicial
- Indicador de progreso: LEDs se rellenan en sentido horario desde LED 1
  - `leds_encendidos = round(porcentaje / 100.0 * 12.0)`
  - Color: azul durante carga → verde flash al 100%
- Al terminar: flash verde completo + vibración 500ms → reinicio automático
- Si falla: flash rojo + vibración corta → vuelve al estado anterior

---

## 12. DIAGRAMA DE TRANSICIONES COMPLETO

```
                         ┌─────────────────┐
                         │   DEEP_SLEEP    │◀──── press largo (cualquier estado)
                         └────────┬────────┘      sleep timeout desde CLOCK
                                  │ tap simple (D8)
                         ┌────────▼────────┐
                         │   WAKING_UP     │ (transitorio)
                         └────────┬────────┘
                          BLE?    │
               ┌──────────────────┼─────────────────────┐
             SÍ│                                       NO│
    ┌──────────▼──────────┐                  ┌───────────▼──────────┐
    │  CLOCK_CONNECTED    │◀─────────────────│  CLOCK_DISCONNECTED  │
    │  (blancos)          │  BLE reconecta   │  (azules)            │
    └──────────┬──────────┘                  └───────────┬──────────┘
               │ press corto                             │ press corto
               ▼                                         ▼
    ┌──────────────────────┐                  ┌────────────────────┐
    │     RADAR_MODE       │◀── doble tap ───▶│   ERROR_NO_GPS     │
    │  (1 LED → dirección) │                  │  (flash rojo 2s)   │
    └──────────┬───────────┘                  └────────────────────┘
               │ doble tap
    ┌──────────▼───────────┐
    │   DISTANCE_MODE      │
    │  (LEDs 1–11 relleno) │
    └──────────┬───────────┘
               │ press corto
               ▼
       CLOCK_CONNECTED

    [Cualquier estado conectado]
    BLE → HAPTIC_RECEIVE ──────────────────▶ HAPTIC_RX ──▶ (estado anterior)
    Doble tap en CLOCK/RADAR/DISTANCE ────▶ HAPTIC_TX ──▶ (estado anterior)

    [Superpuesto a todo]
    Batería < 15% ────────────────────────▶ LOW_BATTERY (overlay)
    Batería < 5%  ────────────────────────▶ DEEP_SLEEP forzado

    [OTA]
    Paquete DFU recibido ─────────────────▶ OTA_MODE ──▶ REBOOT
```

---

## 13. NOTAS DE IMPLEMENTACIÓN IMPORTANTES

1. **TTP223 y rebotes:** Implementar debounce por software de 20ms mínimo. El chip puede generar pulsos espurios al soltar.

2. **LIS3MDL calibración:** El magnetómetro necesita calibración de hard-iron y soft-iron. Implementar rutina de calibración (girar el reloj en 3 ejes) accesible desde la app. Guardar offsets en flash del nRF52840.

3. **BLE y deep sleep:** En el nRF52840, el stack BLE (SoftDevice) debe seguir activo en sleep para mantener advertising. Usar `sd_app_evt_wait()` en lugar de apagado total de CPU.

4. **SK6812 y D10:** Siempre poner D10 HIGH antes de escribir al anillo, y LOW al dormir. Si se escribe con D10=LOW, puede haber comportamiento indefinido.

5. **I2C custom D4/D5:** Inicializar Wire con `Wire.setPins(D4, D5)` antes de cualquier llamada I2C.

6. **⚠️ CRÍTICO — IMU Sense: Low-Power con wake-on-motion (NO Power-Down total).** El LSM6DS3 se configura en Low-Power a 26Hz con el acelerómetro activo y un umbral de ~312mg para wake-on-motion. El giroscopio sí se apaga. El micrófono PDM sí se apaga completamente (`PDM.begin()` + `PDM.end()`). Consumo resultante en DEEP_SLEEP: ~30–35µA. Ver sección 6.6 para código completo y tabla de consumo. El pin INT1 del LSM6DS3 en el XIAO Sense está conectado al GPIO P0.11 del nRF52840 — verificar en el esquemático oficial de Seeed antes de codificar `IMU_INT1_PIN`.

7. **Android Foreground Service — notificación silenciosa:** La notificación persistente del servicio debe crearse en un `NotificationChannel` con `IMPORTANCE_LOW` para que no haga sonido ni aparezca como heads-up. Usar `flutter_foreground_task` que gestiona esto automáticamente. Importante: en Android 14+, el tipo del servicio (`foregroundServiceType`) debe ser `location` y `connectedDevice` simultáneamente; si se declara solo uno, el sistema puede matar el proceso cuando la app pasa a background.

8. **Dynamic Polling — primera ejecución:** En el primer arranque, antes de recibir la ubicación de la pareja, no hay distancia calculada. Arrancar en modo `NEAR` (60s) como valor seguro por defecto, nunca en `PRECISION`, para no drenar la batería antes de saber que hay proximidad real.

9. **Tailscale en móvil:** Los usuarios deben tener Tailscale instalado en sus móviles y el PC, y estar en la misma tailnet. La URL del backend debe ser la IP de Tailscale del PC.

10. **Latencia háptica esperada:** Depende del modo de polling activo en ese momento. En modo PRECISION (RADAR activo, <500m): GPS update (3s) + Supabase Realtime (<100ms) + BLE write (<50ms) ≈ **~3–4s**. En modo REMOTE (>50km): hasta **5–10 minutos** para que la posición se actualice, lo cual es aceptable dado que el toque háptico se envía directamente por Supabase sin esperar al ciclo GPS.

---

*Documento generado como referencia completa pre-implementación. Versión 1.2 — Power-down Sense | Android-only Foreground Service | Dynamic GPS Polling | Rise-to-wake IMU low-power.*
