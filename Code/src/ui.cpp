#include "ui.h"
#include "config.h"
#include "ssd1306.h"
#include "fonts.h"
#include "meter.h"
#include <math.h>

// ===========================================================================
//  LAYOUT - the only pixel coordinates in the firmware. Retune here.
// ===========================================================================
// Vertical stack, from the top: header, power, power bar, swr, scale labels,
// swr bar. Everything above the swr bar sits one row higher than the header
// gap would suggest - that row buys the gap between the scale labels and the
// bar, which otherwise touch.
#if SHOW_POWER_BAR
  #define BAND1_Y   (5  + UI_MARGIN)   // power field
  #define BAND_H    22
  #define PBAR_Y    (28 + UI_MARGIN)   // continuous power bar
  #define BAND2_Y   (31 + UI_MARGIN)   // swr field
#else
  #define BAND1_Y   (5  + UI_MARGIN)
  #define BAND_H    24
  #define BAND2_Y   (30 + UI_MARGIN)
#endif
#define SCALE_Y     (54 + UI_MARGIN)   // swr bar labels (3x5)
#define SBAR_Y      (62 - UI_MARGIN)   // swr bar, off the bottom edge

// horizontal extent of anything that spans the screen
#define BAR_X       UI_MARGIN
#define BAR_LEN     (OLED_W - 2 * UI_MARGIN)
#define BAR_H       2
#define DIV_W       2         // divider notches cut into the swr bar

#define LABEL_W     17        // "PWR" / "SWR" at 5x7
#define UNIT_SLOT   17        // widest unit is "dBm"; fixed so both rows align
#define BLOCK_GAP   6         // label <-> number <-> unit

#define BATT_W      13        // battery icon, matched to the 3x5 version string
#define BATT_H      5
#define BATT_FILL   10

// Calibration has no bars, so it keeps its own fixed geometry rather than
// inheriting the power-bar layout and shifting when SHOW_POWER_BAR changes.
#define CAL_RIGHT   124
#define CAL_BAND1_Y 6
#define CAL_BAND_H  24
#define CAL_BAND2_Y 31

// swr bar ticks, log scale across the full width
static const float TICKS[]  = { 1.0f, 1.5f, 2.0f, 3.0f, 4.0f, 5.0f, 10.0f };
static const char *LABELS[] = { "1", "1.5", "2", "3", "4", "5", "10" };
#define NTICKS 7

// ===========================================================================

// fmtFixed / fmtUint / battPercent live in meter.cpp so they can be unit
// tested on the host. Short aliases keep the drawing code readable.
#define fmt  fmtFixed
#define fmtU fmtUint

static void battery(int16_t x, int16_t y, uint8_t pct) {
    oled::fillRect(x, y, BATT_W - 1, 1, true);
    oled::fillRect(x, y + BATT_H - 1, BATT_W - 1, 1, true);
    oled::fillRect(x, y, 1, BATT_H, true);
    oled::fillRect(x + BATT_W - 2, y, 1, BATT_H, true);
    oled::fillRect(x + BATT_W - 1, y + 1, 1, BATT_H - 2, true);      // nub
    uint8_t n = (uint16_t)BATT_FILL * pct / 100;
    if (n) oled::fillRect(x + 1, y + 1, n, BATT_H - 2, true);
}

static void hbar(int16_t y, float frac, const int16_t *divs, uint8_t ndiv) {
    if (frac >= 0.0f) {
        if (frac > 1.0f) frac = 1.0f;
        oled::fillRect(BAR_X, y, (int16_t)(frac * (BAR_LEN - 1) + 0.5f) + 1, BAR_H, true);
    }
    for (uint8_t i = 0; i < ndiv; i++)
        oled::fillRect(divs[i], y, DIV_W, BAR_H, false);
}

// Peak marker, drawn in the bar's own two rows. The peak is never below the
// live reading, so the marker is always at or right of the fill - which means
// it only needs hiding, never inverting. Below PEAK_GAP of clear space it is
// dropped entirely rather than drawn touching the fill, where it would just
// look like a slightly longer bar.
static void peakMark(int16_t y, float peakFrac, float curFrac) {
    if (peakFrac <= 0.0f) return;
    if (peakFrac > 1.0f) peakFrac = 1.0f;
    if (curFrac  < 0.0f) curFrac  = 0.0f;
    if (curFrac  > 1.0f) curFrac  = 1.0f;

    int16_t mx = BAR_X + (int16_t)(peakFrac * (BAR_LEN - 1) + 0.5f);
    if (mx > BAR_X + BAR_LEN - PEAK_MARKER_W)      // pin at the right edge
        mx = BAR_X + BAR_LEN - PEAK_MARKER_W;

    int16_t barEnd = BAR_X + (int16_t)(curFrac * (BAR_LEN - 1) + 0.5f);
    if (mx - barEnd <= PEAK_GAP) return;           // clamped in - nothing to show

    oled::fillRect(mx, y, PEAK_MARKER_W, BAR_H, true);
}

static void swrBar(float swr) {
    int16_t tx[NTICKS];
    for (uint8_t i = 0; i < NTICKS; i++)
        tx[i] = BAR_X + (int16_t)(log10f(TICKS[i]) * (BAR_LEN - 1) + 0.5f);
    float frac = -1.0f;                                  // < 0 = draw nothing
    if (!isnan(swr)) {
        float s = swr;
        if (isinf(s) || s > SWR_BAR_MAX) s = SWR_BAR_MAX;
        if (s < 1.0f) s = 1.0f;
        frac = log10f(s);
    }
    hbar(SBAR_Y, frac, tx + 1, NTICKS - 2);              // interior ticks only
    for (uint8_t i = 0; i < NTICKS; i++) {
        uint8_t w = font::textW(LABELS[i], font::TINY);
        int16_t x = (i == 0) ? BAR_X
                  : (i == NTICKS - 1) ? BAR_X + BAR_LEN - w
                                      : tx[i] - w / 2;
        font::text(x, SCALE_Y, LABELS[i], font::TINY);
    }
}

// Power number and its unit. No milliwatt mode: the 1N5711s need ~0.33 V to
// conduct, about 100 mW, so anything mW could show is below what the hardware
// can measure. Units are only ever "W" or "dBm".
// Past POWER_LIMIT_WATTS the reading is meaningless anyway - the ADC saturates
// around 28.7 W - so show LIM rather than a number that cannot be trusted. The
// threshold is on watts, so dBm switches over at the same actual power.
static void powerText(char *val, char *unit, float w, float dbm, bool dbmMode) {
    bool lim = w >= POWER_LIMIT_WATTS;
    if (dbmMode) {
        if (lim)            { val[0]='L'; val[1]='I'; val[2]='M'; val[3]=0; }
        else if (w <= 0.0f) { val[0]='-'; val[1]='-'; val[2]='-'; val[3]=0; }
        else                fmt(val, dbm, 1);      // already the native quantity
        unit[0]='d'; unit[1]='B'; unit[2]='m'; unit[3]=0;
        return;
    }
    if (lim) { val[0]='L'; val[1]='I'; val[2]='M'; val[3]=0; }
    else     fmt(val, w, w >= 10.0f ? 1 : 2);
    unit[0]='W'; unit[1]=0;
}

namespace ui {

void splash(float battVolts) {
    oled::clear();
    char b[20], v[10];
    fmt(v, battVolts, 2);
    uint8_t n = 0;
    const char *pre = "Battery: ";
    while (*pre) b[n++] = *pre++;
    for (uint8_t i = 0; v[i]; i++) b[n++] = v[i];
    b[n++] = 'V'; b[n] = 0;

    const char *lines[] = { "QRP Power and", "SWR Meter", "25W max", FW_VERSION, b };
    const int16_t ys[]  = { 4, 14, 27, 39, 51 };
    for (uint8_t i = 0; i < 5; i++) {
        uint8_t w = font::textW(lines[i], font::SMALL);
        font::text(BAR_X + (BAR_LEN - w) / 2, ys[i] + UI_MARGIN, lines[i], font::SMALL);
    }
    oled::flush();
}

void meter(const MeterState &s) {
    oled::clear();

    const uint8_t fw = font::bigFieldW();
    const uint8_t nh = font::bigH();
    const uint8_t total = LABEL_W + BLOCK_GAP + fw + BLOCK_GAP + UNIT_SLOT;
    const int16_t lx = BAR_X + (BAR_LEN - total) / 2;    // block is centred
    const int16_t nx = lx + LABEL_W + BLOCK_GAP;
    const int16_t ux = nx + fw + BLOCK_GAP;

    const int16_t y1 = BAND1_Y + (BAND_H - nh) / 2;
    const int16_t y2 = BAND2_Y + (BAND_H - nh) / 2;
    const int16_t l1 = y1 + (nh - 7) / 2;                // labels centre on the
    const int16_t l2 = y2 + (nh - 7) / 2;                // numeral, not the band

    char val[14], unit[5];

    // header
    font::text(BAR_X, UI_MARGIN, FW_VERSION, font::TINY);
    battery(OLED_W - BATT_W - UI_MARGIN, UI_MARGIN, battPercent(s.battVolts));

    // power row
    powerText(val, unit, s.watts, s.dbm, s.dbmMode);
    // Right-aligned like a reading, including LIM. Only the "---" no-signal
    // placeholder centres. (val[1] distinguishes it from a negative dBm.)
    bool placeholder = (val[0] == '-' && val[1] == '-');
    if (!s.blankPwr)
        font::bigNum(nx, fw, y1, val, placeholder ? font::CENTER : font::RIGHT);
    font::text(lx, l1, "PWR", font::SMALL);
    font::text(ux, l1, unit, font::SMALL);

    // swr row
    if (isnan(s.swr)) {                       // no carrier
        if (!s.blankSwr) font::bigNum(nx, fw, y2, "---", font::CENTER);
    } else {
        // Simply pinned at 9.99, the widest ratio the field holds. Anything
        // past that is "very bad" either way, and it is blinking regardless.
        float v = s.swr;
        if (isinf(v) || v > 9.99f) v = 9.99f;
        fmt(val, v, 2);
        if (!s.blankSwr) font::bigNum(nx, fw, y2, val, font::RIGHT);
        font::text(ux, l2, ":1", font::SMALL);
    }
    font::text(lx, l2, "SWR", font::SMALL);

#if SHOW_POWER_BAR
    const float pwrFrac = s.watts / PWR_BAR_FULL_SCALE;
    hbar(PBAR_Y, pwrFrac, nullptr, 0);                        // continuous
  #if SHOW_PEAK_MARKER
    peakMark(PBAR_Y, s.peakFrac, pwrFrac);
  #endif
#endif
    swrBar(s.swr);

    oled::flush();
}

void cal(const MeterState &s) {
    oled::clear();
    const uint8_t fw = font::bigFieldW();
    const uint8_t nh = font::bigH();
    const int16_t nx = CAL_RIGHT - UI_MARGIN - fw;
    const int16_t y1 = CAL_BAND1_Y + (CAL_BAND_H - nh) / 2;
    const int16_t y2 = CAL_BAND2_Y + (CAL_BAND_H - nh) / 2;

    char v[8];
    fmtU(v, s.fwdRaw); font::bigNum(nx, fw, y1, v, font::RIGHT);
    fmtU(v, s.revRaw); font::bigNum(nx, fw, y2, v, font::RIGHT);
    font::text(BAR_X, y1 + (nh - 7) / 2, "VFWD", font::SMALL);
    font::text(BAR_X, y2 + (nh - 7) / 2, "VREV", font::SMALL);
    oled::flush();
}

}
