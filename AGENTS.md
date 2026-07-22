# AGENTS.md — Hardware Architecture & System Guidelines

Este documento contiene las reglas de arquitectura, hardware, red y decisiones de diseño fundamentales del proyecto **Nexus Halo (Jessi)**. Todo agente o desarrollador que trabaje en esta base de código DEBE leer y respetar strictly estas directrices.

---

## 1. Contexto del Proyecto, Red Tailscale y Modelo de 2 Usuarios

* **Sistema Cerrado Exclusivo para 2 Personas (Pareja):** Este proyecto está conceptualizado y diseñado ÚNICA Y EXCLUSIVAMENTE para sincronizar a dos personas específicas ("Usuario A" y "Usuario B").
* **Infraestructura de Red (Tailscale VPN):** La comunicación entre ambos teléfonos y el servidor Supabase backend se realiza a través de una red privada virtual punto a punto (**Tailscale**). Ambos dispositivos están conectados permanentemente a este túnel de red para garantizar comunicación directa y segura.
* **⚠️ ADVERTENCIA CRÍTICA PARA AGENTES / NO REFACTORIZAR ARQUITECTURA:** 
  - **NO intentar cambiar el sistema a multiusuario, multi-tenant ni añadir registros/autenticación complejos (OAuth, email/password, etc.).**
  - **NO intentar reemplazar la conexión Tailscale ni la comunicación Supabase Realtime por servidores de terceros.**
  - El diseño actual de políticas RLS (`from_user / to_user IN ('A', 'B')`) y la sincronización Realtime vía WebSocket/PostgREST está intencionalmente optimizado para este caso de uso de 2 usuarios y DEBE mantenerse así.

---

## 2. Decisiones de Hardware y Capa Física (Firmware C++)

### 💡 Controlador de LEDs (`nexus_halo/led_controller.cpp`) — SK6812-MINI-E-012
* **Componente:** Anillo de 12 LEDs RGB/RGBW tipo SK6812-MINI-E-012 (compatibles con protocolo NeoPixel).
* **¿Por qué NO se usan librerías estándar como `Adafruit_NeoPixel` o `FastLED`?**
  1. **Interrupción de la pila BLE (SoftDevice):** Las librerías estándar transmiten bits mediante bucles de temporización por CPU desactivando interrupciones (`noInterrupts()`). Desactivar interrupciones por ~480 µs para actualizar 12 LEDs provoca caídas de paquetes de radio Bluetooth Low Energy y fluctuaciones de tiempo en el scheduler de FreeRTOS.
  2. **Precisión de Tiempos por Microsegundo:** El chip SK6812 exige tiempos de pulso extremadamente exactos a una frecuencia de 800 kHz (periodo de 1.25 µs):
     - `T0H` (cero lógico): 250 ns – 400 ns (configurado a 312.5 ns en firmware).
     - `T1H` (uno lógico): 700 ns – 1000 ns (configurado a 937.5 ns en firmware).
     - `Reset Gap`: >80 µs (configurado a 250 µs).
  3. **Transmisión Autónoma por Hardware DMA (`NRF_PWM1` + `EasyDMA`):** El driver está escrito a bajo nivel configurando directamente el periférico `NRF_PWM1` del microcontrolador nRF52840 mediante `EasyDMA`. La CPU solo llena el buffer en RAM (`pwm_buffer`) y el periférico PWM transmite los datos por el pin D10 de forma 100% autónoma por hardware. Esto garantiza **0% de parpadeo (flicker) en los LEDs y 0% de interferencia con el Bluetooth**.
  4. **Bit de Polaridad (`PWM_POLARITY_BIT = 0x8000`):** El periférico PWM del nRF52 por defecto inicia en LOW y conmuta a HIGH. Se DEBE forzar el bit 15 (`0x8000`) en cada muestra PWM para invertir la polaridad (iniciar en HIGH y conmutar a LOW), evitando que el hueco de Reset sea interpretado como nivel ALTO, lo cual corrompería los colores.

---

### 🧭 Brújula / Magnetómetro (`nexus_halo/compass.cpp`) — LIS3MDL
* **Componente:** Magnetómetro de 3 ejes LIS3MDL en bus I2C dedicado (D4 = SDA, D5 = SCL).
* **¿Por qué NO se usa la librería estándar `<Wire.h>` (Hardware TWI)?**
  1. **Ausencia de Resistencias Pull-Up Físicas:** El PCB y cableado del prototipo NO incorporan resistencias pull-up externas (de 4.7 kΩ) en los pines D4 y D5.
  2. **Bloqueo del Hardware I2C:** La librería estándar `Wire` del nRF52 presupone resistencias pull-up físicas en la placa. Sin ellas, las llamadas a `Wire.endTransmission()` se cuelgan indefinidamente o devuelven errores de NACK/Timeout (error TWI 4).
  3. **I2C por Software (Bit-Bang) con Pull-Ups Internos (`INPUT_PULLUP`):** El driver en `compass.cpp` implementa un protocolo I2C por software (Bit-Bang) configurando explícitamente las resistencias pull-up internas del microcontrolador nRF52840 (`GPIO_PIN_CNF_PULL_Pullup` / `INPUT_PULLUP` ~13 kΩ) en los registros del chip (`PIN_CNF`) y alternando la dirección de los registros GPIO (`DIRCLR` / `DIRSET`).
  4. **Estabilidad Comprobada:** Esta implementación por Bit-Bang es el ÚNICO método comprobado que inicializa y lee el sensor LIS3MDL de manera estable sin colgar el bus I2C ni la CPU.

---

### 🔋 Gestión de Energía y Modo de Reposo (`power.cpp`)
* **LEDs de Estado de la Placa XIAO (Active LOW):** Los LEDs integrados (Red, Green, Blue) del XIAO nRF52840 Sense son activos en LOW. Se mantienen en HIGH durante la ejecución normal para ahorrar batería.
* **Fuentes de Despertar de Reposo Profundo (`DEEP_SLEEP`):** El reloj sólo despierta mediante dos fuentes físicas:
  1. Botón capacitivo TTP223 en D8 (interrupción flanco de subida `WAKE_SOURCE_BUTTON`).
  2. Acelerómetro LSM6DS3 en D1 (interrupción por movimiento `WAKE_SOURCE_IMU`).

---

### 🕒 Sincronización Temporal (`nexus_halo.ino` / `timeSyncChar`)
* **Cálculo de Timestamp Unix sin RTC Físico:** El reloj calcula el timestamp Unix dinámico sumando la diferencia transcurrida en `millis()` desde el timestamp base recibido por BLE desde la app (`unix_base_ts + (millis() - millis_at_sync) / 1000`).
* **Secciones Críticas:** Las variables de sincronización temporal son `volatile` y sus lecturas/escrituras están protegidas con `noInterrupts()` / `interrupts()` para evitar desalineación entre bytes durante interrupciones BLE.

---

## 3. Arquitectura Móvil (Flutter App)

* **Servicio en Primer Plano Nativo (`flutter_foreground_task`):** Se ejecuta un servicio nativo permanente en Android para impedir que el sistema operativo destruya el proceso en segundo plano cuando la pantalla del teléfono está apagada.
* **Aislamiento de Isolates en Flutter:** La interfaz de usuario (`main.dart`) y el motor en segundo plano (`background_engine.dart`) se ejecutan en **Isolates separados**. La comunicación entre ambos se realiza exclusivamente mediante puertos de paso de mensajes (`SendPort` / `ReceivePort`).

---

## 4. Reglas de Persistencia y Sincronización
* **Persistencia Diferida en Flash:** Nunca se debe ejecutar `saveToFlash()` de forma síncrona dentro de `updateFromJson()`. Las escrituras en memoria Flash LittleFS demoran 80–100 ms. Los cambios de configuración recibidos por BLE deben actualizar la RAM de inmediato y activar el temporizador diferido de 3 segundos (`config_save_pending`) para escribir en Flash únicamente cuando la transmisión BLE haya finalizado.
* **Cola de Escrituras BLE en App:** La app móvil debe encolar las escrituras GATT en una cola FIFO secuencial (`_enqueueGattWrite`) para prevenir colisiones en Android (GATT Status 133).

---

## 5. Novedades y Mejoras de Arquitectura (v1.1.0)

### 🎯 Haz Simétrico de 3 LEDs para Radar/Brújula
* **Puntero Balanceado:** En `led_controller.cpp` (`showRadar`), la representación visual de la aguja utiliza 3 LEDs simétricos (LED central con brillo máximo ~100% y dos LEDs laterales atenuados según la posición sub-píxel). Esto elimina la asimetría visual de 2 LEDs y proporciona una rotación fluida al girar el reloj.

### 🧭 Calibración de Brújula LIS3MDL de 15 Segundos
* **Duración Ampliada (15s):** Se amplió la ventana de calibración del magnetómetro en `compass.cpp` a 15,000 ms (15 segundos) para capturar un número óptimo de muestras en los 3 ejes ($X, Y, Z$) mientras el usuario realiza el movimiento en 8.
* **Pantalla Dedicada:** Se integró en la app Flutter la pantalla interactiva `Brújula y Calibración LIS3MDL` (accesible en Ajustes -> Categoría 3/Sistema) con temporizador en tiempo real y guardado automático en memoria Flash interna (`compass.dat`).

### 📱 Conectividad BLE y Resiliencia en Android
* **Pre-Escaneo de Liberación:** Antes de conectar directamente por dirección MAC, la app ejecuta un escaneo silencioso de 2 segundos para actualizar la caché de dispositivos de Android OS, evitando desalineaciones y errores `GATT Status 133`.
* **Detención Previa de Escaneo:** Al seleccionar un dispositivo en la pantalla de emparejamiento manual, la app detiene automáticamente cualquier escaneo activo (`stopScan()`) liberando la antena de radio Bluetooth antes de llamar a `connectGatt()`.
* **Auto-Reconexión en Segundo Plano:** El motor de segundo plano (`background_engine.dart`) cuenta con un bucle de reintento automático cada 5 segundos al detectar la desconexión accidental del reloj.
* **Botón de Reconexión Directa:** La pantalla principal (`HomeScreen`) incluye un botón directo de reconexión rápida con la dirección MAC guardada.

### ⚡ Modo OTA / DFU Inalámbrico
* **Reinicio Directo a Bootloader:** La app incorpora un disparador dedicado en la Categoría 5 (Sistema) que envía la orden BLE `0x01` al servicio OTA del reloj. El firmware graba `GPREGRET = 0xA8` y reinicia el microcontrolador nRF52840 directamente en el **Bootloader DFU**, permitiendo flashear firmware inalámbricamente desde herramientas como **nRF Connect**.
