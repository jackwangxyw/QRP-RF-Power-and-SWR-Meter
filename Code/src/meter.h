// ---------------------------------------------------------------------------
//  meter.h - the pure logic: maths, formatting, button timing.
//
//  No hardware calls and no Arduino dependency, so this compiles on the host
//  and is covered by `pio test -e test`. Everything that touches a peripheral
//  lives in main.cpp / ui.cpp / ssd1306.cpp instead.
// ---------------------------------------------------------------------------
#pragma once
#include <stdint.h>

// --- measurement ---------------------------------------------------------
// dBm is the calibrated quantity - the curve is fitted in it - and watts is
// derived. See the calibration block in config.h for why.
float countsToDbm(float counts);            // NAN below the noise floor
float dbmToWatts(float dbm);                // NAN in -> 0 out
float countsToWatts(float counts);          // both of the above

float computeSwr(float fwdW, float revW);   // NAN = no carrier, INF = total
float envelopeStep(float env, float sample, float decay);  // instant attack

// --- formatting ----------------------------------------------------------
// Fixed-point, because avr-libc's printf drops float support unless you link
// the oversized variant.
void fmtFixed(char *buf, float v, uint8_t decimals);
void fmtUint(char *buf, uint16_t v);

// --- battery -------------------------------------------------------------
uint8_t battPercent(float volts);           // interpolated LiPo curve

// --- peak hold -----------------------------------------------------------
// Tracks the highest recent reading as a fraction of the bar's full scale
// rather than in watts, because the bar is linear in pixels: falling at a
// constant rate in fraction is a constant pixel speed, which reads like an
// analogue needle returning. Instant attack, hold, then fall.
struct PeakHold {
    float    frac;         // 0..1 of full scale
    uint32_t holdUntil;
    uint32_t last;         // for the fall's dt

    void  init(uint32_t nowMs);
    float update(float curFrac, uint32_t nowMs);
};

// --- power state ---------------------------------------------------------
// Pure timeout logic; main.cpp owns the panel and the sleep instruction. RF is
// the only thing that wakes the meter out of BLANK or STANDBY - the button is
// deliberately not a wake source, so it is ignored entirely in those states.
enum PowerState : uint8_t { PWR_ACTIVE = 0, PWR_DIM, PWR_BLANK, PWR_STANDBY };

struct PowerManager {
    PowerState state;
    uint32_t   idleSince;

    void       init(uint32_t nowMs);
    void       poke(uint32_t nowMs);       // activity seen - back to ACTIVE
    PowerState update(uint32_t nowMs);
};

// --- button --------------------------------------------------------------
// Both edges debounced. Separated from digitalRead so the timing can be
// driven with synthetic bounce patterns in tests.
enum ButtonEvent : uint8_t { BTN_NONE = 0, BTN_SHORT, BTN_LONG };

struct Button {
    bool     stable;
    bool     raw;
    uint32_t changed;
    uint32_t pressed;
    bool     longFired;

    void init();
    ButtonEvent update(bool downNow, uint32_t nowMs);
};
