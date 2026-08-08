#include "fonts.h"
#include "config.h"
#include "ssd1306.h"
#include "fonts_data.h"

#if BIG_FONT == BIG_FONT_MICHROMA
  #define BF_H BIGF_MICHROMA_H
  #define BF_P BIGF_MICHROMA_PAGES
  #define BF_DIGITW BIGF_MICHROMA_DIGITW
  #define BF_DOTW   BIGF_MICHROMA_DOTW
  #define BF_DASHW  BIGF_MICHROMA_DASHW
  #define BF_GAP    BIGF_MICHROMA_GAP
  #define BF_DIGITS BIGF_MICHROMA_DIGITS
  #define BF_DOT    BIGF_MICHROMA_DOT
  #define BF_DASH   BIGF_MICHROMA_DASH
  #define BF_L      BIGF_MICHROMA_L
  #define BF_I      BIGF_MICHROMA_I
  #define BF_M      BIGF_MICHROMA_M
  #define BF_E      BIGF_MICHROMA_E
  #define BF_R      BIGF_MICHROMA_R
  #define BF_LW     BIGF_MICHROMA_LW
  #define BF_IW     BIGF_MICHROMA_IW
  #define BF_MW     BIGF_MICHROMA_MW
  #define BF_EW     BIGF_MICHROMA_EW
  #define BF_RW     BIGF_MICHROMA_RW
#else
  #define BF_H BIGF_CUSTOM_H
  #define BF_P BIGF_CUSTOM_PAGES
  #define BF_DIGITW BIGF_CUSTOM_DIGITW
  #define BF_DOTW   BIGF_CUSTOM_DOTW
  #define BF_DASHW  BIGF_CUSTOM_DASHW
  #define BF_GAP    BIGF_CUSTOM_GAP
  #define BF_DIGITS BIGF_CUSTOM_DIGITS
  #define BF_DOT    BIGF_CUSTOM_DOT
  #define BF_DASH   BIGF_CUSTOM_DASH
  #define BF_L      BIGF_CUSTOM_L
  #define BF_I      BIGF_CUSTOM_I
  #define BF_M      BIGF_CUSTOM_M
  #define BF_E      BIGF_CUSTOM_E
  #define BF_R      BIGF_CUSTOM_R
  #define BF_LW     BIGF_CUSTOM_LW
  #define BF_IW     BIGF_CUSTOM_IW
  #define BF_MW     BIGF_CUSTOM_MW
  #define BF_EW     BIGF_CUSTOM_EW
  #define BF_RW     BIGF_CUSTOM_RW
#endif

namespace font {

// ------------------------------------------------------------ small fonts
uint8_t textW(const char *s, Small f) {
    uint8_t n = 0; while (s[n]) n++;
    if (!n) return 0;
    uint8_t adv = (f == SMALL) ? F5_ADV : F3_ADV;
    return n * adv - 1;
}

void text(int16_t x, int16_t y, const char *s, Small f) {
    const uint8_t w   = (f == SMALL) ? F5_W   : F3_W;
    const uint8_t adv = (f == SMALL) ? F5_ADV : F3_ADV;
    const uint8_t rows= (f == SMALL) ? 7 : 5;
    for (; *s; s++, x += adv) {
        char c = *s;
        if (c < FONT_FIRST_CHAR || c > FONT_LAST_CHAR) c = '?';
        uint16_t base = (uint16_t)(c - FONT_FIRST_CHAR) * w;
        for (uint8_t col = 0; col < w; col++) {
            uint8_t bits = pgm_read_byte((f == SMALL ? FONT5X7 : FONT3X5) + base + col);
            for (uint8_t r = 0; r < rows; r++)
                if (bits & (1 << r)) oled::pixel(x + col, y + r, true);
        }
    }
}

// ------------------------------------------------------------- numerals
struct Glyph { const uint8_t *data; uint8_t w; };

static Glyph glyphFor(char c) {
    if (c >= '0' && c <= '9')
        return { BF_DIGITS + (uint16_t)(c - '0') * BF_DIGITW * BF_P, BF_DIGITW };
    if (c == '.') return { BF_DOT,  BF_DOTW  };
    // Letters keep their own width. Michroma's M is 28 px against a 21 px
    // digit, and forcing it into the digit cell clipped it into a "V".
    if (c == 'L') return { BF_L, BF_LW };
    if (c == 'I') return { BF_I, BF_IW };
    if (c == 'M') return { BF_M, BF_MW };
    if (c == 'E') return { BF_E, BF_EW };
    if (c == 'R') return { BF_R, BF_RW };
    return          { BF_DASH, BF_DASHW };        // '-' and anything unexpected
}

uint8_t bigH() { return BF_H; }

uint8_t bigW(const char *s) {
    uint8_t w = 0, n = 0;
    for (const char *p = s; *p; p++, n++) w += glyphFor(*p).w;
    return n ? w + BF_GAP * (n - 1) : 0;
}

uint8_t bigFieldW() { return bigW("0.00"); }

// Ink extent of a string within the fixed vertical window, used to centre the
// placeholder optically instead of on the baseline.
static void inkRows(const char *s, uint8_t &top, uint8_t &bot) {
    top = BF_H; bot = 0;
    for (const char *p = s; *p; p++) {
        Glyph g = glyphFor(*p);
        for (uint8_t c = 0; c < g.w; c++)
            for (uint8_t r = 0; r < BF_H; r++)
                if (pgm_read_byte(g.data + c * BF_P + (r >> 3)) & (1 << (r & 7))) {
                    if (r < top) top = r;
                    if (r > bot) bot = r;
                }
    }
    if (top > bot) { top = 0; bot = 0; }
}

void bigNum(int16_t x, uint8_t boxW, int16_t y, const char *s, Align a) {
    uint8_t w = bigW(s);
    int16_t ox = (a == RIGHT)  ? x + boxW - w
               : (a == CENTER) ? x + (boxW - w) / 2
                               : x;
    int16_t oy = y;
    if (a == CENTER) {                       // placeholder: centre on its ink
        uint8_t top, bot; inkRows(s, top, bot);
        oy = y + (BF_H - (bot - top + 1)) / 2 - top;
    }
    for (const char *p = s; *p; p++) {
        Glyph g = glyphFor(*p);
        for (uint8_t c = 0; c < g.w; c++)
            for (uint8_t r = 0; r < BF_H; r++)
                if (pgm_read_byte(g.data + c * BF_P + (r >> 3)) & (1 << (r & 7)))
                    oled::pixel(ox + c, oy + r, true);
        ox += g.w + BF_GAP;
    }
}

}
