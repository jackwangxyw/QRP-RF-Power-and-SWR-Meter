// ---------------------------------------------------------------------------
//  ssd1306.h - 128x64 frame buffer with dirty-page flushing.
//
//  A full frame is 1 KB, which at ~400 kHz takes ~24 ms to clock out and caps
//  the refresh near 40 fps. The panel addresses in 8-row pages, so flush()
//  checksums each page and pushes only the ones that actually changed. A frame
//  where just the bars moved costs 1-2 pages, about 3-6 ms.
// ---------------------------------------------------------------------------
#pragma once
#include "compat.h"

#define OLED_W     128
#define OLED_H     64
#define OLED_PAGES (OLED_H / 8)
#define OLED_BYTES (OLED_W * OLED_PAGES)

namespace oled {

bool begin();                 // false if the panel never acknowledged
void clear();
void flush();                 // push changed pages only
void flushAll();              // force every page

// Power management. The frame buffer and the shadow both survive displayOn
// (false) - nothing is sent to the panel while it is off - so waking needs no
// re-init and no forced flush.
void displayOn(bool on);
void setContrast(uint8_t c);  // 0x00..0xFF, drives segment current

void pixel(int16_t x, int16_t y, bool on);
void fillRect(int16_t x, int16_t y, int16_t w, int16_t h, bool on);

}
