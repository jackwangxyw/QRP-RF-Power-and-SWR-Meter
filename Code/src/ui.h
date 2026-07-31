// ---------------------------------------------------------------------------
//  ui.h - every screen. main.cpp computes values and calls in here; it holds
//  no drawing code, and this file holds no measurement code.
//
//  All pixel coordinates live in one block at the top of ui.cpp.
// ---------------------------------------------------------------------------
#pragma once
#include "compat.h"

struct MeterState {
    float    watts;        // forward power
    float    swr;          // NAN = no carrier, INFINITY = total reflection
    float    battVolts;
    float    peakFrac;     // peak marker, 0..1 of PWR_BAR_FULL_SCALE; 0 = none
    uint16_t fwdRaw;       // rounded 12-bit counts, for calibration mode
    uint16_t revRaw;
    bool     dbm;          // unit toggle
    bool     blankPwr;     // warning blink, off phase
    bool     blankSwr;
};

namespace ui {
void splash(float battVolts);
void meter(const MeterState &s);
void cal(const MeterState &s);
}
