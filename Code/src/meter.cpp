#include "meter.h"
#include "config.h"
#include <math.h>

// --- measurement ---------------------------------------------------------

float countsToDbm(float x) {
    if (x < (float)NOISE_FLOOR_COUNTS) return NAN;
    float l = log10f(x);
    float d = CAL_A;
    d = d * l + CAL_B;
    d = d * l + CAL_C;
    d = d * l + CAL_D;
    d = d * l + CAL_E;
    return d + CAL_OFFSET_DB;
}

float dbmToWatts(float dbm) {
    if (isnan(dbm)) return 0.0f;
    float w = powf(10.0f, (dbm - 30.0f) * 0.1f);
    return w > 0.0f ? w : 0.0f;          // a polynomial fit can dip negative
}

float countsToWatts(float x) { return dbmToWatts(countsToDbm(x)); }

float computeSwr(float fwd, float rev) {
    if (fwd < SWR_MIN_POWER) return NAN;
    if (rev >= fwd)          return INFINITY;
    float g = sqrtf(rev / fwd);
    if (g > 0.999f) g = 0.999f;
    return (1.0f + g) / (1.0f - g);
}

// Fraction of the gap to close in dtMs at a tauMs time constant. Linear stand-in
// for 1-exp(-dt/tau) - within a few percent while dt < tau, and clamped to 1
// beyond that so a long gap between samples simply lands on the new value
// instead of overshooting. No expf() on an 8-bit core for this.
static float timeCoeff(uint16_t dtMs, uint16_t tauMs) {
    if (tauMs == 0 || dtMs >= tauMs) return 1.0f;
    return (float)dtMs / (float)tauMs;
}

float envelopeMs(float env, float sample, uint16_t dtMs, uint16_t tauMs) {
    if (sample >= env) return sample;                       // instant attack
    return env + (sample - env) * timeCoeff(dtMs, tauMs);
}

float readoutMs(float y, float sample, uint16_t dtMs, uint16_t tauMs) {
    return y + (sample - y) * timeCoeff(dtMs, tauMs);       // symmetric
}

// --- formatting ----------------------------------------------------------

void fmtFixed(char *buf, float v, uint8_t dec) {
    if (isnan(v)) { buf[0]='-'; buf[1]='-'; buf[2]='-'; buf[3]=0; return; }
    bool neg = v < 0.0f;
    if (neg) v = -v;
    uint32_t mult = 1;
    for (uint8_t i = 0; i < dec; i++) mult *= 10;
    if (isinf(v) || v > 400000.0f / mult) {
        buf[0]='O'; buf[1]='V'; buf[2]='R'; buf[3]=0; return;
    }
    uint32_t scaled = (uint32_t)(v * mult + 0.5f);
    uint32_t whole = scaled / mult, frac = scaled % mult;
    char *p = buf;
    if (neg && scaled) *p++ = '-';
    char t[11]; uint8_t n = 0;
    if (!whole) t[n++] = '0';
    while (whole) { t[n++] = (char)('0' + whole % 10); whole /= 10; }
    while (n) *p++ = t[--n];
    if (dec) {
        *p++ = '.';
        for (int8_t i = (int8_t)dec - 1; i >= 0; i--) {
            uint32_t d = 1;
            for (int8_t k = 0; k < i; k++) d *= 10;
            *p++ = (char)('0' + (frac / d) % 10);
        }
    }
    *p = 0;
}

void fmtUint(char *buf, uint16_t v) {
    char t[6]; uint8_t n = 0;
    if (!v) t[n++] = '0';
    while (v) { t[n++] = (char)('0' + v % 10); v /= 10; }
    char *p = buf;
    while (n) *p++ = t[--n];
    *p = 0;
}

// --- battery -------------------------------------------------------------
// A linear map would read ~60% for most of the cell's life because of the
// 3.75-3.95 V plateau, so this interpolates a real discharge curve.
uint8_t battPercent(float v) {
    static const float   V[] = {3.27f,3.61f,3.69f,3.75f,3.80f,3.84f,3.87f,3.98f,4.08f,4.20f};
    static const uint8_t P[] = {0,     5,    10,   25,   40,   50,   60,   75,   85,  100};
    if (v <= V[0]) return 0;
    for (uint8_t i = 1; i < 10; i++)
        if (v <= V[i])
            return (uint8_t)(P[i-1] + (v - V[i-1]) / (V[i] - V[i-1]) * (P[i] - P[i-1]));
    return 100;
}

// --- peak hold -----------------------------------------------------------

void PeakHold::init(uint32_t now) { frac = 0.0f; holdUntil = now; last = now; }

float PeakHold::update(float cur, uint32_t now) {
    if (cur < 0.0f) cur = 0.0f;
    uint32_t dt = now - last;
    last = now;

    if (cur >= frac) {                       // new peak - latch and re-arm
        frac      = cur;
        holdUntil = now + PEAK_HOLD_MS;
    } else if ((int32_t)(now - holdUntil) >= 0) {
        frac -= (PEAK_FALL_WPS / PWR_BAR_FULL_SCALE) * (float)dt * 0.001f;
        if (frac < cur) frac = cur;          // never fall through the reading
    }
    return frac;
}

// --- power state ---------------------------------------------------------

void PowerManager::init(uint32_t now) { state = PWR_ACTIVE; idleSince = now; }
void PowerManager::poke(uint32_t now) { state = PWR_ACTIVE; idleSince = now; }

PowerState PowerManager::update(uint32_t now) {
    uint32_t idle = now - idleSince;
    if      (idle >= SLEEP_STANDBY_MS) state = PWR_STANDBY;
    else if (idle >= SLEEP_BLANK_MS)   state = PWR_BLANK;
    else if (idle >= SLEEP_DIM_MS)     state = PWR_DIM;
    else                               state = PWR_ACTIVE;
    return state;
}

// --- button --------------------------------------------------------------

void Button::init() {
    stable = raw = longFired = false;
    changed = pressed = 0;
}

// Both edges are debounced: the raw level must hold steady for
// BUTTON_DEBOUNCE_MS before it counts. Timing the release from the press
// instead would let a momentary contact break mid-hold register as a real
// release, firing a spurious short press and restarting the long-press timer.
ButtonEvent Button::update(bool down, uint32_t t) {
    ButtonEvent ev = BTN_NONE;

    if (down != raw) { raw = down; changed = t; }

    if (raw != stable && t - changed >= BUTTON_DEBOUNCE_MS) {
        stable = raw;
        if (stable)          { pressed = t; longFired = false; }
        else if (!longFired) { ev = BTN_SHORT; }
    }

    // fire on crossing the threshold, not on release - feels much better
    if (stable && !longFired && t - pressed >= BUTTON_LONG_PRESS_MS) {
        longFired = true;
        ev = BTN_LONG;
    }
    return ev;
}
