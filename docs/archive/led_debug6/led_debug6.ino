// =============================================================================
// led_debug6.ino
// Standalone SK6812 ring debugger — Seeed XIAO nRF52840
// Fixes validated here were ported into nexus_halo v2.2 (led_controller.cpp).
//
// D7  = data pin   D10 = MOSFET gate (HIGH = ring on)
//
// NO BLE, NO state machine, NO complex dependencies.
// Starts HFCLK crystal manually so PWM timing is accurate.
// (Main firmware uses SoftDevice for HFCLK — no manual start needed there.)
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

// ---- PWM timing (SK6812MINI-E-012 @ 800 kHz) --------------------------------
// PRESCALER=0 → 16 MHz clock → 1 tick = 62.5 ns
// COUNTERTOP=20 → period = 1.25 µs (800 kHz)
// T0H = 5 ticks = 312.5 ns (datasheet range 250-400ns)
// T1H = 15 ticks = 937.5 ns (datasheet range 700-1000ns)
// Reset  >200 µs → 200 × 1.25 µs = 250 µs ✓
#define PWM_COUNTERTOP  20
#define PWM_T0H          5
#define PWM_T1H         15
#define PWM_RESET       200   // halfwords of LOW (reset gap)

// ---- Buffers ----------------------------------------------------------------
static uint16_t pwm_buf[LED_COUNT * 24 + PWM_RESET];
static uint8_t  r_buf[LED_COUNT];
static uint8_t  g_buf[LED_COUNT];
static uint8_t  b_buf[LED_COUNT];

#include <math.h>

static uint8_t  g_brightness = 30;  // percent, 0-100 — but see MAX_BRIGHTNESS_PCT below
#define MAX_BRIGHTNESS_PCT 40  // hardware is designed for ≤40% on a 3.7V LiPo — don't exceed this

// Converts the 0-100% user-facing brightness into the 0-255 byte value
// actually sent to the LEDs, with gamma correction so the percentage FEELS
// linear to the eye. Human brightness perception is roughly a power curve,
// not linear with the raw PWM/grayscale value — without this, the jump from
// e.g. 40% to 50% looks much bigger than 10% to 20% did.
static uint8_t brightness_byte() {
    if (g_brightness == 0) return 0;
    float pct = g_brightness / 100.0f;
    float gamma_corrected = powf(pct, 2.2f);
    uint8_t b = (uint8_t)(gamma_corrected * 255.0f + 0.5f);
    return (b == 0) ? 1 : b; // any nonzero % should show SOMETHING
}

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

    // NOTE: previously experimented with forcing high drive strength (H0H1,
    // then H0S1) here, theorizing standard drive couldn't slew the chain's
    // capacitive load fast enough. Dropping that: the library sketch uses
    // Adafruit's stock nRF52 PWM driver, which does NOT touch drive strength
    // and runs on the plain default (standard, S0S1) — and it works
    // flawlessly on this exact board. That's direct evidence the drive
    // strength was never the problem, so leaving it at the pinMode() default
    // instead of continuing to guess at a value.

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
// IMPORTANT: bit 15 (0x8000) of every PWM compare value selects "start HIGH,
// go LOW at the compare count" polarity. Without it, the nRF52 PWM peripheral
// defaults to the OPPOSITE polarity (start LOW, go HIGH at compare count) —
// which is backwards for WS2812/SK6812 and was the actual bug: it made the
// "reset" gap (compare=0) drive the line HIGH for 250µs instead of LOW, and
// turned every data bit's pulse inside-out. Adafruit's own nRF52 NeoPixel
// driver ORs this bit onto every value for exactly this reason.
#define PWM_POLARITY_BIT 0x8000

static void update_buf(uint8_t t1h = PWM_T1H) {
    int idx = 0;
    for (int i = 0; i < LED_COUNT; i++) {
        // SK6812 wire order: Green byte, Red byte, Blue byte
        uint8_t ch[3] = { g_buf[i], r_buf[i], b_buf[i] };
        for (int c = 0; c < 3; c++) {
            for (int bit = 7; bit >= 0; bit--) {
                pwm_buf[idx++] = ((ch[c] & (1 << bit)) ? t1h : (uint8_t)PWM_T0H) | PWM_POLARITY_BIT;
            }
        }
    }
    while (idx < LED_COUNT * 24 + PWM_RESET) {
        pwm_buf[idx++] = PWM_POLARITY_BIT; // compare=0 with correct polarity → LOW for reset gap
    }
}

static void show(uint8_t t1h = PWM_T1H) {
    update_buf(t1h);

    // Re-enable PWM if it was stopped by the SEQEND0→STOP shortcut from a
    // previous call.  Must be done before writing SEQ registers.
    NRF_PWM1->ENABLE = 1;

    NRF_PWM1->SEQ[0].PTR      = (uint32_t)pwm_buf;
    NRF_PWM1->SEQ[0].CNT      = LED_COUNT * 24 + PWM_RESET; // count of 16-bit halfwords
    NRF_PWM1->SEQ[0].REFRESH  = 0;
    NRF_PWM1->SEQ[0].ENDDELAY = 0;

    NRF_PWM1->EVENTS_SEQEND[0] = 0;
    NRF_PWM1->EVENTS_STOPPED   = 0;
    NRF_PWM1->TASKS_SEQSTART[0] = 1;

    // Block until SEQEND fires (DMA done, last WS2812 bit clocked out).
    // Actual frame time for 12 LEDs = 288 bits × 1.25 µs + 250 µs reset ≈ 0.61 ms.
    // Safety timeout = 20 ms.
    uint32_t deadline = millis() + 20;
    while (!NRF_PWM1->EVENTS_SEQEND[0] && (int32_t)(millis() - deadline) < 0) {}

    if (!NRF_PWM1->EVENTS_SEQEND[0]) {
        Serial.println("[PWM] !! SEQEND never fired — DMA stuck or PWM mis-configured!");
        return;
    }

    // Wait for peripheral to actually stop (SHORTS wired SEQEND0→STOP).
    // This ensures the pin is LOW / idle before the next frame starts.
    deadline = millis() + 5;
    while (!NRF_PWM1->EVENTS_STOPPED && (int32_t)(millis() - deadline) < 0) {}
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
    Serial.println("  T=T1H sweep  +=brighter  -=dimmer  q=REALISTIC (4 LEDs, your real design)");
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
    set_all(brightness_byte(), brightness_byte(), brightness_byte());
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
        uint8_t br = brightness_byte();
        switch (c) {
        case '1': g_test=1; Serial.println("-> All RED");   digitalWrite(PIN_LED_POWER,HIGH); set_all(br,0,0);  show(); break;
        case '2': g_test=2; Serial.println("-> All GREEN"); digitalWrite(PIN_LED_POWER,HIGH); set_all(0,br,0);  show(); break;
        case '3': g_test=3; Serial.println("-> All BLUE");  digitalWrite(PIN_LED_POWER,HIGH); set_all(0,0,br);  show(); break;
        case '4': g_test=4; Serial.println("-> All WHITE"); digitalWrite(PIN_LED_POWER,HIGH); set_all(br,br,br);show(); break;
        case '5':
            g_test=5; Serial.println("-> All OFF");
            clear_all(); show();
            // Protocol-level "off" alone leaves a faint leakage glow on LED1
            // on this hardware (same issue your library sketch worked around
            // by cutting D10) — physically kill LED power too.
            digitalWrite(PIN_LED_POWER, LOW);
            break;
        case '6': g_test=6; Serial.println("-> Colour sweep"); digitalWrite(PIN_LED_POWER,HIGH); break;
        case '7': g_test=7; Serial.println("-> Chase"); digitalWrite(PIN_LED_POWER,HIGH); break;
        case '9': g_test=9; Serial.println("-> LED walk (yellow)"); digitalWrite(PIN_LED_POWER,HIGH); break;
        case 't': g_test=10;Serial.println("-> GRB order test (LED 0 only)"); digitalWrite(PIN_LED_POWER,HIGH); break;
        case 'T': g_test=11;Serial.println("-> T1H timing sweep 6..14"); digitalWrite(PIN_LED_POWER,HIGH); break;
        case '0': g_test=0; Serial.println("-> Auto-cycle"); digitalWrite(PIN_LED_POWER,HIGH); break;
        case 'q': g_test=12;Serial.println("-> REALISTIC test: only 4 LEDs, your actual product scenario"); digitalWrite(PIN_LED_POWER,HIGH); break;
        case '+':
            g_brightness = (g_brightness + 10 <= MAX_BRIGHTNESS_PCT) ? g_brightness + 10 : MAX_BRIGHTNESS_PCT;
            Serial.print("Brightness = "); Serial.print(g_brightness); Serial.println("%");
            break;
        case '-':
            g_brightness = (g_brightness >= 10) ? g_brightness - 10 : 0;
            Serial.print("Brightness = "); Serial.print(g_brightness); Serial.println("%");
            break;
        }

        // Re-apply brightness immediately to whatever static mode (1-5) is
        // currently showing, instead of waiting for the next mode keypress.
        // Previously +/- only updated g_brightness; nothing redrew the ring
        // until you pressed a color key again.
        if (g_test >= 1 && g_test <= 5) {
            uint8_t nb = brightness_byte();
            switch (g_test) {
            case 1: set_all(nb,0,0);   break;
            case 2: set_all(0,nb,0);   break;
            case 3: set_all(0,0,nb);   break;
            case 4: set_all(nb,nb,nb); break;
            case 5: clear_all();       break;
            }
            show();
        }
    }

    uint8_t br = brightness_byte();

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

    // ---- REALISTIC test: only 4 LEDs lit at once (your actual product's
    // real-world max), cycling R/G/B/W — this is the scenario that
    // actually matters for the finished product on a 3.7V LiPo, unlike
    // every other test mode here which deliberately stresses worst-case
    // (12 LEDs, up to 100%) to probe the driver.
    case 12: {
        static uint32_t ms = 0;
        static int phase = 0;
        if (now - ms >= 1000) {
            ms = now;
            clear_all();
            uint8_t r=0,g=0,b=0;
            switch (phase % 4) {
            case 0: r=br; break;
            case 1: g=br; break;
            case 2: b=br; break;
            case 3: r=br; g=br; b=br; break;
            }
            // 4 LEDs, evenly spaced around the 12-LED ring
            set_led(0, r,g,b);
            set_led(3, r,g,b);
            set_led(6, r,g,b);
            set_led(9, r,g,b);
            show();
            Serial.print("[REALISTIC 4-LED] phase="); Serial.println(phase % 4);
            phase++;
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

    // ---- T1H timing sweep: all-red with T1H = 10..18 (3 s each) -----------
    // SK6812MINI-E-012 datasheet T1H range is 700-1000ns (typ 900ns).
    // 10-11 ticks (625-682ns) are BELOW spec and expected to show bleed/errors.
    // 12-16 ticks (744-1000ns) is the legal window — 15 (937ns) is now default.
    // Find the value where all 12 LEDs look cleanly red with no colour bleed.
    case 11: {
        static uint32_t ms = 0;
        static uint8_t t1h = 10;
        if (now - ms >= 3000) {
            ms = now;
            set_all(br, 0, 0);
            show(t1h);
            Serial.print("[T1H] T1H="); Serial.print(t1h);
            Serial.print("  ("); Serial.print(t1h*62); Serial.print("ns)");
            Serial.println("  — all should look RED. Good? note this value.");
            t1h++;
            if (t1h > 18) t1h = 10;
        }
        break;
    }

    } // end switch
}
