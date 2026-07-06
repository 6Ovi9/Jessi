# Nexus Halo Firmware v2.2 — Changelog

**Date:** 2026-07-06
**Base:** v2.1
**Files changed:** `led_controller.cpp`, `led_controller.h`, `runtime_config.h`, `nexus_halo.ino` (version string)

---

## Summary

v2.2 is a hardware-correctness and UX polish release. The LED ring now works for the first time under direct-register PWM (it was previously silent due to four compounding bugs). The clock display has been redesigned for low-brightness operation. The sleep timeout setting from the app now actually takes effect.

---

## Bug Fixes

### 1. PWM Polarity Bit Missing — `led_controller.cpp` 🔴 CRITICAL

**Symptom:** LEDs never lit up under direct-register PWM driver.

**Root cause:** The nRF52 PWM peripheral defaults to "start LOW, go HIGH at compare count" — the opposite of what SK6812/WS2812 requires. Every compare value must have bit 15 (0x8000) set to select the correct polarity. Without it, every data bit's pulse is inside-out and the reset gap drives the line HIGH for 250 µs instead of LOW.

**Fix:** Added `#define PWM_POLARITY_BIT 0x8000` and ORed it onto every `pwm_buffer[]` write, including the reset-gap words. This is identical to the fix Adafruit's own nRF52 NeoPixel driver applies.

---

### 2. Wrong DECODER.LOAD Value — `led_controller.cpp` 🔴 CRITICAL

**Symptom:** Every frame was corrupted; EVENTS_SEQEND never fired.

**Root cause:** `NRF_PWM1->DECODER` was set to `(3UL << 0)` with a comment claiming this was "LOAD=Common". In fact, per the nRF52840 Product Specification §6.24:
- `LOAD=0` = Common — one halfword per period (correct for NeoPixel)
- `LOAD=3` = Waveform — four halfwords per period; 4th overrides COUNTERTOP

LOAD=3 caused the DMA to consume the entire buffer in a fraction of the intended time and produced completely garbled output. SEQEND never fired.

**Fix:** Changed `(3UL << 0)` to `(0UL << 0)`.

---

### 3. T1H Below Datasheet Minimum — `led_controller.cpp` 🔴 CRITICAL

**Symptom:** High bits were marginal even after polarity and DECODER fixes.

**Root cause:** `PWM_T1H` was set to 10 ticks (625 ns). The SK6812MINI-E-012 datasheet specifies a T1H minimum of 700 ns. The code comment incorrectly cited 600 ns from the generic SK6812 datasheet, not the MINI-E-012 variant.

**Fix:** Changed `PWM_T1H` from 10 → 15 ticks = 937.5 ns, within the 700–1000 ns spec window. Validated via full timing sweep (10–18 ticks) in `led_debug6`.

---

### 4. No SEQEND→STOP Shortcut — `led_controller.cpp` 🟡

**Symptom:** PWM peripheral stayed active after each frame; rapid show() calls could race.

**Root cause:** `NRF_PWM1->SHORTS` was never configured. The peripheral kept running after the sequence ended, so the next TASKS_SEQSTART could overlap with an active transfer.

**Fix:**
- Added `NRF_PWM1->SHORTS = PWM_SHORTS_SEQEND0_STOP_Msk` in `begin()`.
- Added `NRF_PWM1->ENABLE = 1` at the top of `show()` to re-enable after each auto-stop.
- Added EVENTS_STOPPED wait after EVENTS_SEQEND to ensure pin is LOW before the next frame.

---

### 5. Sleep Timeout Overflow — `runtime_config.h` 🔴

**Symptom:** Changing sleep/clock timeout in the app had no visible effect, or produced wrong (very short) timeouts.

**Root cause:** `clockTimeoutS` and `sleepTimeoutS` were stored as `uint8_t` (max 255). Any value > 255 seconds sent by the app (e.g. 300 s = 5 min) silently wrapped: 300 % 256 = 44 — the watch slept after 44 seconds regardless of what the app sent.

**Fix:** Widened both fields to `uint16_t` (max 65535 s).

NOTE: Config magic bumped v6 → v7. On first boot after flashing, the old flash config is discarded and the firmware resets to defaults. Re-send your preferred timeout from the app once to persist it.

---

## Feature Changes

### 6. Clock Display Redesign — `led_controller.cpp` 🟢

#### Seconds hand — crossfade removed, snaps every 5 s

Previous implementation used sqrtf() interpolation to crossfade between two adjacent LEDs. At ≤30% brightness the fractional glow was visually distracting and added no practical readability benefit.

New behaviour: seconds hand snaps cleanly to the LED for the current 5-second window (`second / 5 % 12`). 12 LEDs × 5 s = 60 s full circle.

#### Minute hand — single LED → two adjacent LEDs at equal brightness

Previously the minute hand lit one LED per 5-minute block (12 discrete positions per hour, no sub-5-minute indication).

New behaviour: both the current 5-minute-block LED and the next LED are lit at equal brightness. Encodes "you are between X:05 and X:10" without fractional interpolation. Max LEDs on at any time: 4 (hour + 2×minute + second) — within hardware power budget.

---

### 7. Gamma Correction — `led_controller.cpp` 🟢

Added `_brightnessFromPct()` private helper applying gamma 2.2. Active only when the `logarithmicBrightness` flag is enabled via the app. Falls back to linear `(pct * 255) / 100` when off.

Applied to: showClock(), showRadar(), showDistance(), errorNoGPS(), errorBattery(), successOTA().

---

## Files Changed

| File | Change |
|------|--------|
| `led_controller.cpp` | PWM polarity, DECODER.LOAD, T1H, SEQEND→STOP, clock display redesign, gamma helper |
| `led_controller.h` | Added `_brightnessFromPct()` private declaration |
| `runtime_config.h` | clockTimeoutS/sleepTimeoutS uint8_t→uint16_t; magic v6→v7 |
| `nexus_halo.ino` | Version string 2.1 → 2.2 |
| `README.md` | Version references updated |
| `CHANGELOG_v2.2.md` | This file (NEW) |

---

## Migration Notes

| Item | Action |
|------|--------|
| Flash config (timeouts) | Reset to defaults on first boot — re-send from app |
| led_debug6 sketch | Standalone debugger; polarity/timing already correct there, no changes needed |
| HFCLK | No manual start needed in main firmware — SoftDevice manages it |
| Series resistor on D7 | Still not needed — confirmed not the bottleneck in debug sessions |
