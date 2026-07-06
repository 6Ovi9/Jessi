/*
  ============================================================================
  LED Ring Diagnostic Sketch for Couples Watch (v1.4 - Ultra Smooth & Interactivo)
  ============================================================================
  Este sketch soluciona las transiciones escalonadas (jerky steps) aumentando la
  tasa de refresco a 100 Hz (10ms) y aplicando un desvanecimiento sinusoidal.

  También apaga físicamente la alimentación de los LEDs (D10 = LOW) al detenerlos
  para evitar corrientes de fuga que mantengan encendido el LED 0 de forma residual.
  ============================================================================
*/

#include <Adafruit_NeoPixel.h>

#define LED_PIN          D7   // Pin de datos de los LEDs
#define LED_POWER_PIN    D10  // Pin del MOSFET de alimentación de los LEDs
#define NUM_LEDS         12   // Cantidad de LEDs en el anillo
#define NEO_TYPE         (NEO_GRB + NEO_KHZ800)

#ifndef PI
#define PI 3.14159265358979323846
#endif

Adafruit_NeoPixel pixels(NUM_LEDS, LED_PIN, NEO_TYPE);

// Variables de estado ajustables en tiempo real
uint8_t max_brightness = 30; // Brillo inicial seguro

enum TestMode {
  MODE_STOPPED,
  MODE_SWEEP,
  MODE_CROSSFADE,
  MODE_RAINBOW,
  MODE_SOLID,
  MODE_SINGLE_FADE
};

TestMode current_mode = MODE_SWEEP;
uint32_t last_update_ms = 0;

// Variables de estados internos para animaciones
int sweep_led_index = 0;
int sweep_color_index = 0;

// Mapeo horario
int getClockwiseLED(int step) {
  return (12 - step) % 12;
}

// Generador de colores para arcoíris
uint32_t Wheel(byte WheelPos, uint8_t brightness) {
  WheelPos = 255 - WheelPos;
  uint8_t r, g, b;
  if(WheelPos < 85) {
    r = 255 - WheelPos * 3;
    g = 0;
    b = WheelPos * 3;
  } else if(WheelPos < 170) {
    WheelPos -= 85;
    r = 0;
    g = WheelPos * 3;
    b = 255 - WheelPos * 3;
  } else {
    WheelPos -= 170;
    r = WheelPos * 3;
    g = 255 - WheelPos * 3;
    b = 0;
  }
  r = (r * brightness) / 255;
  g = (g * brightness) / 255;
  b = (b * brightness) / 255;
  return pixels.Color(r, g, b);
}

void printMenu() {
  Serial.println("\n==============================================");
  Serial.println("  MENÚ INTERACTIVO DE DIAGNÓSTICO LED v1.4");
  Serial.println("==============================================");
  Serial.print("  [Brillo Actual]: "); Serial.print(max_brightness); Serial.println("/255");
  Serial.print("  [Modo Activo]:   ");
  switch(current_mode) {
    case MODE_STOPPED:     Serial.println("APAGADO (MOSFET desconectado)"); break;
    case MODE_SWEEP:       Serial.println("1. Barrido Rápido (1 LED a la vez - Colores Puros)"); break;
    case MODE_CROSSFADE:   Serial.println("2. Crossfade Completo (12 LEDs - Suave a 100Hz)"); break;
    case MODE_RAINBOW:     Serial.println("3. Arcoíris Rotativo (12 LEDs - Suave a 100Hz)"); break;
    case MODE_SOLID:       Serial.println("4. Color Sólido Morado (12 LEDs - Test Ruido PWM)"); break;
    case MODE_SINGLE_FADE: Serial.println("5. Crossfade en un Solo LED (LED 0 - Suave a 100Hz)"); break;
  }
  Serial.println("----------------------------------------------");
  Serial.println("  Comandos del Serial Monitor (envía una tecla):");
  Serial.println("   '1' -> Barrido rápido individual");
  Serial.println("   '2' -> Crossfade sinusoidal en todo el anillo (12 LEDs)");
  Serial.println("   '3' -> Arcoíris rotativo suave (12 LEDs)");
  Serial.println("   '4' -> Color Morado sólido (R+B) en todo el anillo");
  Serial.println("   '5' -> Crossfade sinusoidal en un solo LED (LED 0)");
  Serial.println("   '0' o 's' -> Apagar todos los LEDs (Corta alimentación)");
  Serial.println("   '+' -> Aumentar brillo (+5)");
  Serial.println("   '-' -> Disminuir brillo (-5)");
  Serial.println("==============================================\n");
}

void setup() {
  Serial.begin(115200);
  while (!Serial && millis() < 5000) delay(10);
  delay(500);

  // MOSFET D10 para alimentación
  pinMode(LED_POWER_PIN, OUTPUT);
  digitalWrite(LED_POWER_PIN, HIGH);
  delay(100);

  pixels.begin();
  pixels.setBrightness(255); // Escalado manual por software para precisión
  pixels.clear();
  pixels.show();

  printMenu();
}

void updateSweep(uint32_t now) {
  if (now - last_update_ms < 120) return;
  last_update_ms = now;

  pixels.clear();
  int physical_led = getClockwiseLED(sweep_led_index);
  
  uint8_t r = 0, g = 0, b = 0;
  if (sweep_color_index == 0)      r = max_brightness;
  else if (sweep_color_index == 1) g = max_brightness;
  else                             b = max_brightness;

  pixels.setPixelColor(physical_led, pixels.Color(r, g, b));
  pixels.show();

  sweep_led_index++;
  if (sweep_led_index >= NUM_LEDS) {
    sweep_led_index = 0;
    sweep_color_index = (sweep_color_index + 1) % 3;
  }
}

void updateCrossfade(uint32_t now) {
  // Frecuencia de actualización de 100 Hz (cada 10ms)
  if (now - last_update_ms < 10) return;
  last_update_ms = now;

  // Ciclo completo (Rojo -> Verde -> Azul -> Rojo) en 3000ms
  const uint32_t cycle_time = 3000;
  uint32_t progress = now % cycle_time;
  
  float pos = (float)progress / (cycle_time / 3.0f); // 0.0 a 3.0
  int phase = (int)floor(pos);
  float f = pos - phase;
  
  // Transición sinusoidal (suavizado al inicio y final del fade)
  float f_smooth = (1.0f - cos(f * PI)) / 2.0f;

  uint8_t colors[3][3] = {
    {max_brightness, 0, 0}, // Rojo
    {0, max_brightness, 0}, // Verde
    {0, 0, max_brightness}  // Azul
  };

  int next_phase = (phase + 1) % 3;
  uint8_t r = colors[phase][0] + (colors[next_phase][0] - colors[phase][0]) * f_smooth;
  uint8_t g = colors[phase][1] + (colors[next_phase][1] - colors[phase][1]) * f_smooth;
  uint8_t b = colors[phase][2] + (colors[next_phase][2] - colors[phase][2]) * f_smooth;

  for (int i = 0; i < NUM_LEDS; i++) {
    pixels.setPixelColor(i, pixels.Color(r, g, b));
  }
  pixels.show();
}

void updateRainbow(uint32_t now) {
  // Frecuencia de actualización de 100 Hz (cada 10ms)
  if (now - last_update_ms < 10) return;
  last_update_ms = now;

  // Una vuelta completa del arcoíris dura 2500ms
  const uint32_t rotation_period = 2500;
  uint32_t progress = now % rotation_period;
  uint8_t hue_offset = (progress * 256) / rotation_period;

  for (int i = 0; i < NUM_LEDS; i++) {
    byte led_hue = (i * 256 / NUM_LEDS + hue_offset) & 255;
    pixels.setPixelColor(i, Wheel(led_hue, max_brightness));
  }
  pixels.show();
}

void updateSolid(uint32_t now) {
  if (now - last_update_ms < 50) return;
  last_update_ms = now;
  
  for (int i = 0; i < NUM_LEDS; i++) {
    pixels.setPixelColor(i, pixels.Color(max_brightness, 0, max_brightness));
  }
  pixels.show();
}

void updateSingleFade(uint32_t now) {
  // Frecuencia de actualización de 100 Hz (cada 10ms)
  if (now - last_update_ms < 10) return;
  last_update_ms = now;

  // Ciclo completo (Rojo -> Verde -> Azul -> Rojo) en 3000ms
  const uint32_t cycle_time = 3000;
  uint32_t progress = now % cycle_time;
  
  float pos = (float)progress / (cycle_time / 3.0f); // 0.0 a 3.0
  int phase = (int)floor(pos);
  float f = pos - phase;
  
  float f_smooth = (1.0f - cos(f * PI)) / 2.0f;

  uint8_t colors[3][3] = {
    {max_brightness, 0, 0}, // Rojo
    {0, max_brightness, 0}, // Verde
    {0, 0, max_brightness}  // Azul
  };

  int next_phase = (phase + 1) % 3;
  uint8_t r = colors[phase][0] + (colors[next_phase][0] - colors[phase][0]) * f_smooth;
  uint8_t g = colors[phase][1] + (colors[next_phase][1] - colors[phase][1]) * f_smooth;
  uint8_t b = colors[phase][2] + (colors[next_phase][2] - colors[phase][2]) * f_smooth;

  pixels.clear();
  pixels.setPixelColor(0, pixels.Color(r, g, b)); // Solo el LED 0
  pixels.show();
}

void loop() {
  if (Serial.available() > 0) {
    char c = Serial.read();
    
    if (c == '\n' || c == '\r') return;

    bool state_changed = true;
    bool enable_power = false;
    
    switch(c) {
      case '1': current_mode = MODE_SWEEP; enable_power = true; break;
      case '2': current_mode = MODE_CROSSFADE; enable_power = true; break;
      case '3': current_mode = MODE_RAINBOW; enable_power = true; break;
      case '4': current_mode = MODE_SOLID; enable_power = true; break;
      case '5': current_mode = MODE_SINGLE_FADE; enable_power = true; break;
      case '0':
      case 's': 
        current_mode = MODE_STOPPED; 
        pixels.clear();
        pixels.show();
        digitalWrite(LED_POWER_PIN, LOW); // Cortar corriente físicamente
        Serial.println("[POWER] MOSFET Desconectado (D10 = LOW). LEDs apagados al 100%.");
        state_changed = true;
        break;
      case '+': 
        if (max_brightness <= 250) max_brightness += 5;
        else max_brightness = 255;
        Serial.print("[INFO] Brillo aumentado a: "); Serial.println(max_brightness);
        state_changed = false;
        break;
      case '-': 
        if (max_brightness >= 10) max_brightness -= 5;
        else max_brightness = 5;
        Serial.print("[INFO] Brillo disminuido a: "); Serial.println(max_brightness);
        state_changed = false;
        break;
      default:
        state_changed = false;
        break;
    }

    if (enable_power) {
      digitalWrite(LED_POWER_PIN, HIGH);
      delay(15); // Estabilizar línea de alimentación
      Serial.println("[POWER] MOSFET Conectado (D10 = HIGH).");
    }

    if (state_changed) {
      pixels.clear();
      pixels.show();
      printMenu();
    }
  }

  uint32_t now = millis();
  switch(current_mode) {
    case MODE_SWEEP:       updateSweep(now); break;
    case MODE_CROSSFADE:   updateCrossfade(now); break;
    case MODE_RAINBOW:     updateRainbow(now); break;
    case MODE_SOLID:       updateSolid(now); break;
    case MODE_SINGLE_FADE: updateSingleFade(now); break;
    case MODE_STOPPED:     break;
  }
}
