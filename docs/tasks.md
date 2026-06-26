# LED Ring Fix — NEXUS HALO

Bugs identificados en el diagnóstico de los LEDs SK6812-MINI-E. El hardware está fijo (PCB ya fabricada), todos los cambios son por software.

---

## Tareas

### 1. Quitar `show()` interno de `clear()`
**Archivo:** `led_controller.cpp`

`clear()` actualmente llama a `pixels.show()`, lo que provoca un frame negro entre cada refresco del reloj. Hay que convertirlo en una operación de buffer únicamente.

```cpp
void LEDController::clear() {
  for (int i = 0; i < LED_COUNT; i++) {
    pixels.setPixelColor(i, 0, 0, 0);
  }
  // Eliminar: show();
}
```

---

### 2. Arreglar `setPower()` tras el cambio de `clear()`
**Archivo:** `led_controller.cpp`

Al quitar el `show()` de `clear()`, `setPower(false)` deja de apagar los LEDs físicamente. Hay que transmitir el apagado antes de bajar el MOSFET, y añadir un delay de estabilización al encenderlo.

```cpp
void LEDController::setPower(bool on) {
  power_on = on;
  if (on) {
    digitalWrite(PIN_LED_POWER, HIGH);
    delay(10); // Esperar a que Vcc se estabilice antes de transmitir
  } else {
    // Transmitir todo a cero ANTES de cortar corriente
    for (int i = 0; i < LED_COUNT; i++) {
      pixels.setPixelColor(i, 0, 0, 0);
    }
    pixels.show();
    delayMicroseconds(50);
    digitalWrite(PIN_LED_POWER, LOW);
  }
}
```

Esto también resuelve el LED 0 que se quedaba encendido con luz residual al apagar.

---

### 3. Bajar frecuencia de refresco del reloj de 200Hz a 30Hz
**Archivo:** `nexus_halo.ino` — funciones `handleStateClockConnected()` y `handleStateClockDisconnected()`

El intervalo de 5ms (200Hz) es excesivo, genera picos de corriente continuos en la línea de alimentación y no aporta fluidez visible. Con la interpolación de milisegundos que ya existe en `showClock()`, 33ms es más que suficiente.

```cpp
// ANTES
if ((now_ms - last_time_update_ms) >= 5) {

// DESPUÉS
if ((now_ms - last_time_update_ms) >= 33) {
```

Aplicar en ambas funciones (`handleStateClockConnected` y `handleStateClockDisconnected`).

---

### 4. Verificar todos los `clear()` explícitos que esperaban un `show()`
**Archivos:** `led_controller.cpp`, `nexus_halo.ino`

Tras el cambio del punto 1, hay que revisar cada llamada a `clear()` en el código y confirmar que el `show()` posterior sigue existiendo. Los sitios a revisar:

- `begin()` → OK, `clear()` seguido de nada crítico
- `showClock()` → OK, ya tiene `show()` al final
- `showRadar()` → OK, ya tiene `show()` al final  
- `showDistance()` → OK, ya tiene `show()` al final
- `updateOTAProgress()` → OK, ya tiene `show()` al final
- `handleStateCalibration()` en el `.ino` → OK, tiene `led_controller.show()` explícito

---

## Contexto del hardware

- MCU: Seeed XIAO nRF52840
- LEDs: 12× SK6812-MINI-E en anillo, formato `NEO_GRB + NEO_KHZ800`
- Alimentación: LiPo 3.7V → MOSFET (D10) → anillo
- Desacoplo: 100nF entre cada LED, 10µF en el punto de soldadura de la batería
- Sin condensador bulk en el anillo → los picos de `show()` no se pueden absorber por hardware, hay que minimizarlos por software
