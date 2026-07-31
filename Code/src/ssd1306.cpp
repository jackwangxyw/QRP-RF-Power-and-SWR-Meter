#include "ssd1306.h"
#include "config.h"
#include "i2c.h"

#define CTRL_CMD  0x00
#define CTRL_DATA 0x40

static uint8_t buf[OLED_BYTES];
static uint8_t shadow[OLED_BYTES];        // last bytes actually sent
static uint8_t addr = OLED_ADDRESS;

static void cmd(uint8_t c) {
    i2c::beginTx(addr); i2c::write(CTRL_CMD); i2c::write(c); i2c::endTx();
}
static void cmd2(uint8_t c, uint8_t a) {
    i2c::beginTx(addr); i2c::write(CTRL_CMD); i2c::write(c); i2c::write(a); i2c::endTx();
}

// A byte-exact compare against what was last sent.
//
// This started out as a 16-byte Fletcher-16 per page to save RAM. The screen
// simulator caught it colliding on the very first frame pair it was given:
// going from "25.0" to "0.00" changes 12 bytes of page 0 but leaves both
// Fletcher sums identical, so the page was never pushed and the panel kept
// showing the old glyph tops. Moving ink sideways within a row is exactly the
// case Fletcher is blind to, and it is the common case for a numeric readout.
// A 1 KB shadow costs a third of RAM but is exact, and we have it to spare.
static bool pageChanged(uint8_t page) {
    const uint16_t off = (uint16_t)page * OLED_W;
    for (uint8_t i = 0; i < OLED_W; i++)
        if (buf[off + i] != shadow[off + i]) return true;
    return false;
}

static void keepPage(uint8_t page) {
    const uint16_t off = (uint16_t)page * OLED_W;
    for (uint8_t i = 0; i < OLED_W; i++) shadow[off + i] = buf[off + i];
}

static void window(uint8_t page) {
    i2c::beginTx(addr);
    i2c::write(CTRL_CMD);
    i2c::write(0x21); i2c::write(0); i2c::write(OLED_W - 1);   // column range
    i2c::endTx();
    i2c::beginTx(addr);
    i2c::write(CTRL_CMD);
    i2c::write(0x22); i2c::write(page); i2c::write(page);      // page range
    i2c::endTx();
}

static void sendPage(uint8_t page) {
    window(page);
    const uint8_t *p = buf + (uint16_t)page * OLED_W;
    uint8_t sent = 0;
    while (sent < OLED_W) {
        uint8_t n = OLED_W - sent;
        if (n > I2C_TX_CHUNK) n = I2C_TX_CHUNK;
        i2c::beginTx(addr);
        i2c::write(CTRL_DATA);
        for (uint8_t i = 0; i < n; i++) i2c::write(p[sent + i]);
        i2c::endTx();
        sent += n;
    }
}

namespace oled {

bool begin() {
    i2c::begin();
    if      (i2c::probe(OLED_ADDRESS)) addr = OLED_ADDRESS;
    else if (i2c::probe(OLED_ADDRESS == 0x3C ? 0x3D : 0x3C))
                                       addr = (OLED_ADDRESS == 0x3C ? 0x3D : 0x3C);
    else return false;

    cmd(0xAE);                 // off
    cmd2(0xD5, 0x80);          // clock divide
    cmd2(0xA8, 0x3F);          // multiplex = 64
    cmd2(0xD3, 0x00);          // offset
    cmd(0x40);                 // start line
    cmd2(0x8D, 0x14);          // charge pump on
    cmd2(0x20, 0x00);          // horizontal addressing
    cmd(0xA1);                 // segment remap
    cmd(0xC8);                 // COM scan direction
    cmd2(0xDA, 0x12);          // COM pin config
    cmd2(0x81, OLED_CONTRAST_ACTIVE);
    cmd2(0xD9, 0xF1);          // pre-charge
    cmd2(0xDB, 0x40);          // VCOMH
    cmd(0xA4);                 // resume from RAM
    cmd(0xA6);                 // not inverted
    cmd(0xAF);                 // on

    clear();
    flushAll();
    return true;
}

void clear() { for (uint16_t i = 0; i < OLED_BYTES; i++) buf[i] = 0; }

void flush() {
    for (uint8_t p = 0; p < OLED_PAGES; p++) {
        if (!pageChanged(p)) continue;
        keepPage(p);
        sendPage(p);
    }
}

void flushAll() {
    for (uint8_t p = 0; p < OLED_PAGES; p++) {
        keepPage(p);
        sendPage(p);
    }
}

void displayOn(bool on)      { cmd(on ? 0xAF : 0xAE); }
void setContrast(uint8_t c)  { cmd2(0x81, c); }

void pixel(int16_t x, int16_t y, bool on) {
    if ((uint16_t)x >= OLED_W || (uint16_t)y >= OLED_H) return;
    uint16_t i = (uint16_t)(y >> 3) * OLED_W + (uint16_t)x;
    uint8_t  m = 1 << (y & 7);
    if (on) buf[i] |= m; else buf[i] &= ~m;
}

void fillRect(int16_t x, int16_t y, int16_t w, int16_t h, bool on) {
    for (int16_t cy = y; cy < y + h; cy++)
        for (int16_t cx = x; cx < x + w; cx++) pixel(cx, cy, on);
}

}
