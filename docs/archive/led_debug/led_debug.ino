// =============================================================================
// led_debug.ino
// Standalone SK6812 ring debugger — Seeed XIAO nRF52840
//
// D7  = data pin   D10 = MOSFET gate (HIGH = ring on)
//
// NO BLE, NO state machine, NO complex dependencies.
// Starts HFCLK crystal manually so PWM timing is accurate.
//
// Serial @ 115200 baud (open AFTER flashing, board appears as USB COM port):
//   keys 1-9, t, T, +, -   (see loop() for full list)
// =============================================================================

// On Seeed nRF52 board package the USB-CDC Serial object needs TinyUSB.
// Including bluefruit pulls it in; alternatively we declare the minimum here.
#include <bluefruit.h>   // pulls in Serial (TinyUSB CDC) — we never call Bluefruit.begin()

// ---- Hardware config --------------------------------------------------------
#define PIN_LED_DATA   D7
#define PIN_LED_POWER  D10
#define LED_COUNT      12

// ---- PWM timing (SK6812MINI-E RGBW @ 800 kHz) --------------------------------
// PRESCALER=0 → 16 MHz clock → 1 tick = 62.5 ns
// COUNTERTOP=20 → period = 1.25 µs (800 kHz)
// T0H spec 300 ns →  5 ticks = 312.5 ns  ✓
// T1H spec 600 ns → 11 ticks = 687.5 ns  ✓
// Reset  >80 µs  → 300 × 1.25 µs = 375 µs ✓
//
// *** SK6812-MINI-E is RGBW (4-channel) → 32 bits (4 bytes) per LED ***
// Previously only 24 bits (3 bytes) were sent per LED. That caused a cascading
// 1-byte offset down the chain — the R byte of LED N landed in the G slot of
// LED N+1, making every colour look reddish and the last LED leak green.
// Fix: send GRBW (4 bytes), keeping W=0 for RGB-only tests.
#define PWM_COUNTERTOP  20
#define PWM_T0H          5
#define PWM_T1H         11
#define PWM_RESET       300   // halfwords of LOW (reset gap)
#define LED_BITS        32    // bits per LED: 4 bytes (G,R,B,W) × 8

// ---- Buffers ----------------------------------------------------------------
static uint16_t pwm_buf[LED_COUNT * LED_BITS + PWM_RESET];
static uint8_t  r_buf[LED_COUNT];
static uint8_t  g_buf[LED_COUNT];
static uint8_t  b_buf[LED_COUNT];
static uint8_t  w_buf[LED_COUNT]; // White channel — send 0 to keep it off

static uint8_t  g_brightness = 80;

// =============================================================================
// HFCLK — must be started manually when SoftDevice is NOT active
// Without this, the PWM peripheral uses the imprecise HFRC and bit timing
// at 800 kHz is unreliable, causing all LEDs to appear off or corrupt.
// =============================================================================
static void hfclk_start() {
    if (NRF_CLOCK->HFCLKSTAT & CLOCK_HFCLKSTAT_SRC_Msk) return; // already on XTAL
    NRF_CLOCK->EVENTS_HFCLKSTARTED = 0;
    NRF_CLOCK->TASKS_HFCLKSTART    = 1;
    uint32_t t = millis();
    while (!NRF_CLOCK->EVENTS_HFCLKSTARTED && millis() - t < 200) {}
    Serial.print("[CLK] HFCLK source: ");
    Serial.println((NRF_CLOCK->HFCLKSTAT & CLOCK_HFCLKSTAT_SRC_Msk) ? "XTAL 64MHz" : "RC (fallback)");
}

// =============================================================================
// PWM init
// =============================================================================
static void pwm_init() {
    uint32_t pin_num = g_ADigitalPinMap[PIN_LED_DATA];

    // Ensure data pin starts LOW before PWM takes over — the SK6812 needs
    // a defined LOW idle state before the first frame.
    pinMode(PIN_LED_DATA, OUTPUT);
    digitalWrite(PIN_LED_DATA, LOW);

    NRF_PWM1->ENABLE = 0;

    // Connect channel 0 to data pin; disconnect all others (bit 31 = disconnect)
    NRF_PWM1->PSEL.OUT[0] = pin_num;
    NRF_PWM1->PSEL.OUT[1] = 0x80000000UL;
    NRF_PWM1->PSEL.OUT[2] = 0x80000000UL;
    NRF_PWM1->PSEL.OUT[3] = 0x80000000UL;

    NRF_PWM1->MODE       = 0;  // Up counting
    NRF_PWM1->PRESCALER  = 0;  // Divide-by-1 → 16 MHz → 62.5 ns per tick
    NRF_PWM1->COUNTERTOP = PWM_COUNTERTOP; // 20 ticks → 1.25 µs period (800 kHz)
    NRF_PWM1->LOOP       = 0;  // Fire once per SEQSTART

    // DECODER.LOAD values (nRF52840 PS §6.24):
    //   0 = Common     — ONE 16-bit halfword drives all 4 channels identically.
    //                    This is what we want: each halfword in pwm_buf is one
    //                    PWM period for the single data pin (channel 0).
    //   1 = Grouped    — TWO halfwords per period (ch0&1 share, ch2&3 share).
    //   2 = Individual — FOUR halfwords per period (one per channel).
    //   3 = Waveform   — FOUR halfwords per period; the 4th overrides COUNTERTOP.
    //                    *** Using 3 here corrupts every frame and SEQEND       ***
    //                    *** never fires because the DMA sees wrong data size.  ***
    NRF_PWM1->DECODER = (0UL << 0); // LOAD=Common(0) | MODE=RefCountTop(0)

    // Auto-stop after each sequence so repeated SEQSTART calls don't race.
    NRF_PWM1->SHORTS = PWM_SHORTS_SEQEND0_STOP_Msk;

    NRF_PWM1->ENABLE = 1;

    Serial.print("[PWM] NRF_PWM1 ready. Pin P");
    Serial.print(pin_num >> 5);
    Serial.print(".");
    Serial.print(pin_num & 0x1F);
    Serial.print("  T0H="); Serial.print(PWM_T0H * 62, DEC); Serial.print("ns");
    Serial.print("  T1H="); Serial.print(PWM_T1H * 62, DEC); Serial.println("ns");
}

// =============================================================================
// Build DMA buffer and fire one frame
// =============================================================================
static void update_buf(uint8_t t1h = PWM_T1H) {
    int idx = 0;
    for (int i = 0; i < LED_COUNT; i++) {
        // SK6812 wire order: Green byte, Red byte, Blue byte
        uint8_t ch[3] = { g_buf[i], r_buf[i], b_buf[i] };
        for (int c = 0; c < 3; c++) {
            for (int bit = 7; bit >= 0; bit--) {
                pwm_buf[idx++] = (ch[c] & (1 << bit)) ? t1h : (uint8_t)PWM_T0H;
            }
        }
    }
    while (idx < LED_COUNT * 24 + PWM_RESET) {
        pwm_buf[idx++] = 0; // LOW for reset gap
    }
}

static void show(uint8_t t1h = PWM_T1H) {
    update_buf(t1h);

    // ---- Nordic SDK required start-up sequence (bare-metal version) ----------
    // The nrfx_pwm driver always does:
    //   STOP → wait STOPPED → DISABLE (ENABLE=0) → ENABLE (ENABLE=1) → SEQSTART
    // Skipping DISABLE leaves the peripheral in a "stopped-but-enabled" limbo
    // state where subsequent SEQSTART commands are silently ignored — the DMA
    // counter fires and SEQEND fires, but the PWM output is never updated.
    // That is exactly the "always shows the first frame's colour" bug.
    NRF_PWM1->ENABLE = 0;
    __DMB();   // data-memory barrier: ensure ENABLE=0 is committed before ENABLE=1
    NRF_PWM1->ENABLE = 1;

    NRF_PWM1->SEQ[0].PTR      = (uint32_t)pwm_buf;
    NRF_PWM1->SEQ[0].CNT      = LED_COUNT * 24 + PWM_RESET; // count of 16-bit halfwords
    NRF_PWM1->SEQ[0].REFRESH  = 0;
    NRF_PWM1->SEQ[0].ENDDELAY = 0;

    NRF_PWM1->EVENTS_SEQEND[0] = 0;
    NRF_PWM1->EVENTS_STOPPED   = 0;
    __DMB();   // ensure all writes above reach RAM before DMA starts reading
    NRF_PWM1->TASKS_SEQSTART[0] = 1;

    // Block until SEQEND fires (DMA done, last bit clocked out).
    // Frame time for 12 LEDs = 288 bits × 1.25 µs + 375 µs reset ≈ 0.74 ms.
    // Safety timeout = 20 ms.
    uint32_t deadline = millis() + 20;
    while (!NRF_PWM1->EVENTS_SEQEND[0] && (int32_t)(millis() - deadline) < 0) {}

    if (!NRF_PWM1->EVENTS_SEQEND[0]) {
        Serial.println("[PWM] !! SEQEND never fired — DMA stuck or PWM mis-configured!");
        return;
    }

    // Wait for STOPPED (triggered automatically by SEQEND0→STOP shortcut).
    deadline = millis() + 5;
    while (!NRF_PWM1->EVENTS_STOPPED && (int32_t)(millis() - deadline) < 0) {}

    // *** DISABLE after stop — required before the next frame's ENABLE=1 ***
    // Without this the peripheral re-enters the "stopped-but-enabled" limbo.
    NRF_PWM1->ENABLE = 0;
}

// =============================================================================
// Colour helpers
// =============================================================================
static void set_all(uint8_t r, uint8_t g, uint8_t b) {
    for (int i = 0; i < LED_COUNT; i++) { r_buf[i]=r; g_buf[i]=g; b_buf[i]=b; }
}
static void set_led(int i, uint8_t r, uint8_t g, uint8_t b) {
    if (i < 0 || i >= LED_COUNT) return;
    r_buf[i]=r; g_buf[i]=g; b_buf[i]=b;
}
static void clear_all() { set_all(0,0,0); }

// =============================================================================
// SETUP
// =============================================================================
void setup() {
    Serial.begin(115200);
    // Wait up to 3 s for USB-CDC enumeration, but don't block forever
    uint32_t t = millis();
    while (!Serial && millis() - t < 3000) {}

    Serial.println("\n========================================");
    Serial.println("  Nexus Halo LED Debug v1.1");
    Serial.println("========================================");
    Serial.println("  1=Red  2=Green  3=Blue  4=White  5=Off");
    Serial.println("  6=Sweep  7=Chase  9=Walk  t=GRB test");
    Serial.println("  T=T1H sweep  +=brighter  -=dimmer");
    Serial.println("  (default: auto R→G→B→W→off every 1 s)");
    Serial.println("========================================\n");

    // Start HFCLK crystal — essential for accurate PWM at 800 kHz
    hfclk_start();

    // Power MOSFET — keep LOW until PWM is configured
    pinMode(PIN_LED_POWER, OUTPUT);
    digitalWrite(PIN_LED_POWER, LOW);

    // Configure NRF_PWM1
    pwm_init();

    // Power on ring and send a clean reset frame (all LOW for 250 µs)
    clear_all();
    digitalWrite(PIN_LED_POWER, HIGH);
    delay(10); // let MOSFET and LED VCC stabilise
    show();    // send reset (all-zero buf = all LOW)
    delay(10);

    // First visible test: all white so we know it works
    set_all(g_brightness, g_brightness, g_brightness);
    show();
    Serial.println("[SETUP] Done. Showing WHITE. If LEDs are dark, DMA or MOSFET issue.");
}

// =============================================================================
// LOOP
// =============================================================================
static int g_test = 0; // 0 = auto-cycle

void loop() {
    uint32_t now = millis();

    // ---- Serial commands ----------------------------------------------------
    if (Serial.available()) {
        char c = Serial.read();
        uint8_t br = g_brightness;
        switch (c) {
        case '1': g_test=1; Serial.println("-> All RED");   set_all(br,0,0);  show(); break;
        case '2': g_test=2; Serial.println("-> All GREEN"); set_all(0,br,0);  show(); break;
        case '3': g_test=3; Serial.println("-> All BLUE");  set_all(0,0,br);  show(); break;
        case '4': g_test=4; Serial.println("-> All WHITE"); set_all(br,br,br);show(); break;
        case '5': g_test=5; Serial.println("-> All OFF");   clear_all();      show(); break;
        case '6': g_test=6; Serial.println("-> Colour sweep"); break;
        case '7': g_test=7; Serial.println("-> Chase"); break;
        case '9': g_test=9; Serial.println("-> LED walk (yellow)"); break;
        case 't': g_test=10;Serial.println("-> GRB order test (LED 0 only)"); break;
        case 'T': g_test=11;Serial.println("-> T1H timing sweep 6..14"); break;
        case '0': g_test=0; Serial.println("-> Auto-cycle"); break;
        case '+':
            g_brightness = (g_brightness <= 235) ? g_brightness + 20 : 255;
            Serial.print("Brightness = "); Serial.println(g_brightness);
            break;
        case '-':
            g_brightness = (g_brightness >= 20) ? g_brightness - 20 : 0;
            Serial.print("Brightness = "); Serial.println(g_brightness);
            break;
        }
    }

    uint8_t br = g_brightness;

    switch (g_test) {

    // ---- Auto-cycle: R→G→B→W→off, 1 s each --------------------------------
    case 0: {
        static uint32_t ms = 0;
        static int ph = 0;
        if (now - ms >= 1000) {
            ms = now;
            ph = (ph+1) % 5;
            switch (ph) {
            case 0: set_all(br,0,0);   show(); Serial.println("[AUTO] RED");   break;
            case 1: set_all(0,br,0);   show(); Serial.println("[AUTO] GREEN"); break;
            case 2: set_all(0,0,br);   show(); Serial.println("[AUTO] BLUE");  break;
            case 3: set_all(br,br,br); show(); Serial.println("[AUTO] WHITE"); break;
            case 4: clear_all();       show(); Serial.println("[AUTO] OFF");   break;
            }
        }
        break;
    }

    // ---- Static colour modes 1-5 handled on keypress, nothing in loop ------
    case 1: case 2: case 3: case 4: case 5:
        delay(100);
        break;

    // ---- Colour sweep: each LED a different hue ----------------------------
    case 6: {
        clear_all();
        for (int i = 0; i < LED_COUNT; i++) {
            float h = (float)i / LED_COUNT * 6.0f;
            int hi = (int)h;
            float f = h - hi;
            uint8_t p = (uint8_t)(br*(1-f)), q = (uint8_t)(br*f);
            uint8_t r2,g2,b2;
            switch (hi%6) {
            case 0: r2=br;g2=q; b2=0;  break;
            case 1: r2=p; g2=br;b2=0;  break;
            case 2: r2=0; g2=br;b2=q;  break;
            case 3: r2=0; g2=p; b2=br; break;
            case 4: r2=q; g2=0; b2=br; break;
            default:r2=br;g2=0; b2=p;  break;
            }
            set_led(i, r2, g2, b2);
        }
        show();
        delay(100);
        break;
    }

    // ---- Chase: R=current, G=next, B=prev ----------------------------------
    case 7: {
        static uint32_t ms = 0;
        static int idx = 0;
        if (now - ms >= 200) {
            ms = now;
            clear_all();
            set_led(idx,                       br, 0,  0);   // red
            set_led((idx+1)%LED_COUNT,         0,  br, 0);   // green
            set_led((idx-1+LED_COUNT)%LED_COUNT,0,  0,  br); // blue
            show();
            Serial.print("[CHASE] LED "); Serial.print(idx);
            Serial.print("(R)  "); Serial.print((idx+1)%LED_COUNT);
            Serial.print("(G)  "); Serial.print((idx-1+LED_COUNT)%LED_COUNT);
            Serial.println("(B)");
            idx = (idx+1)%LED_COUNT;
        }
        break;
    }

    // ---- Walk: one yellow LED moves around the ring ------------------------
    case 9: {
        static uint32_t ms = 0;
        static int idx = 0;
        if (now - ms >= 600) {
            ms = now;
            clear_all();
            set_led(idx, br, br, 0); // yellow
            show();
            Serial.print("[WALK] LED "); Serial.println(idx);
            idx = (idx+1)%LED_COUNT;
        }
        break;
    }

    // ---- GRB order test: cycle R/G/B on LED 0 only -------------------------
    case 10: {
        static uint32_t ms = 0;
        static int ph = 0;
        if (now - ms >= 1500) {
            ms = now;
            clear_all();
            switch (ph%4) {
            case 0:
                r_buf[0]=br; g_buf[0]=0;  b_buf[0]=0;
                Serial.println("[GRB] r_buf=ON → expect RED on LED 0");   break;
            case 1:
                r_buf[0]=0;  g_buf[0]=br; b_buf[0]=0;
                Serial.println("[GRB] g_buf=ON → expect GREEN on LED 0"); break;
            case 2:
                r_buf[0]=0;  g_buf[0]=0;  b_buf[0]=br;
                Serial.println("[GRB] b_buf=ON → expect BLUE on LED 0");  break;
            case 3:
                Serial.println("[GRB] all=0    → expect OFF");            break;
            }
            show();
            ph++;
        }
        break;
    }

    // ---- T1H timing sweep: all-red with T1H = 6..14 (3 s each) -----------
    // Find the value where all LEDs look cleanly red with no colour bleed.
    case 11: {
        static uint32_t ms = 0;
        static uint8_t t1h = 6;
        if (now - ms >= 3000) {
            ms = now;
            set_all(br, 0, 0);
            show(t1h);
            Serial.print("[T1H] T1H="); Serial.print(t1h);
            Serial.print("  ("); Serial.print(t1h*62); Serial.print("ns)");
            Serial.println("  — all should look RED. Good? note this value.");
            t1h++;
            if (t1h > 14) t1h = 6;
        }
        break;
    }

    } // end switch
}
