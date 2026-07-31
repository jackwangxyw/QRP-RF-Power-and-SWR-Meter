// ---------------------------------------------------------------------------
//  fonts.h - drawing on top of the generated tables in fonts_data.h.
//
//    SMALL (5x7)  labels, units, splash, calibration
//    TINY  (3x5)  version string and swr bar labels only
//    big          the numerals, selected by BIG_FONT in config.h
//
//  Numerals are tabular in BOTH axes: every digit occupies an identical cell
//  and every string crops to the same vertical window. A right-aligned reading
//  therefore cannot shift as the value changes.
// ---------------------------------------------------------------------------
#pragma once
#include "compat.h"

namespace font {

enum Small : uint8_t { SMALL, TINY };
enum Align : uint8_t { LEFT, RIGHT, CENTER };

uint8_t textW(const char *s, Small f);
void    text(int16_t x, int16_t y, const char *s, Small f);

uint8_t bigH();
uint8_t bigW(const char *s);
uint8_t bigFieldW();          // width of a full 4-glyph reading, eg "0.00"

// Draws s inside the box [x, x+boxW). RIGHT for readings; CENTER for the
// "---" placeholder, which also centres on its own ink so it sits on the
// optical middle rather than the digit baseline.
void bigNum(int16_t x, uint8_t boxW, int16_t y, const char *s, Align a);

}
