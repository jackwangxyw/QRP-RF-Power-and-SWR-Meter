#include "i2c.h"
#include "config.h"

#if I2C_USE_HARDWARE
// ---------------------------------------------------------------------------
//  Hardware TWI0. Unusable on Rev 2 (PB0/PB1 are unconnected) but kept wired
//  up so a future board rev is a one-line change in config.h. Untested.
// ---------------------------------------------------------------------------
#include <Wire.h>
namespace i2c {
void begin()                { Wire.swap(0); Wire.begin(); Wire.setClock(400000); }
void beginTx(uint8_t a)     { Wire.beginTransmission(a); }
void write(uint8_t b)       { Wire.write(b); }
bool endTx()                { return Wire.endTransmission() == 0; }
bool probe(uint8_t a)       { Wire.beginTransmission(a); return Wire.endTransmission() == 0; }
}
#else
// ---------------------------------------------------------------------------
//  Software I2C on PA6 (SDA) / PA7 (SCL), confirmed against the silkscreen.
//
//  Open drain by construction: a line is pulled low by making it an output
//  (its OUT bit is always 0) and released by making it an input so the pull-up
//  takes over. Neither line is ever actively driven high.
//
//  No I2C pull-ups on the PCB - this leans on the OLED module's 4.7k, with
//  the ATtiny's internal ~35k as a weak backup.
// ---------------------------------------------------------------------------
#include <util/delay.h>

#define SDA_bm PIN6_bm
#define SCL_bm PIN7_bm

static inline void sdaLow()     { VPORTA.DIR |=  SDA_bm; }
static inline void sdaRelease() { VPORTA.DIR &= ~SDA_bm; }
static inline void sclLow()     { VPORTA.DIR |=  SCL_bm; }
static inline bool sdaRead()    { return VPORTA.IN & SDA_bm; }
static inline bool sclRead()    { return VPORTA.IN & SCL_bm; }
static inline void hold()       { _delay_us(I2C_HALF_US); }

// Release SCL and wait for it to rise. Covers clock stretching and stops us
// hanging forever if a line is shorted.
static void sclRelease() {
    VPORTA.DIR &= ~SCL_bm;
    uint8_t guard = 200;
    while (!sclRead() && guard--) _delay_us(1);
}

static bool txAck;

static bool rawWrite(uint8_t b) {
    for (uint8_t i = 0; i < 8; i++) {
        if (b & 0x80) sdaRelease(); else sdaLow();
        b <<= 1;
        hold(); sclRelease(); hold(); sclLow(); hold();
    }
    sdaRelease();                             // ninth clock: read the ack
    hold(); sclRelease(); hold();
    bool ack = !sdaRead();
    sclLow(); hold();
    return ack;
}

static void rawStart() { sdaRelease(); sclRelease(); hold(); sdaLow(); hold(); sclLow(); hold(); }
static void rawStop()  { sdaLow(); hold(); sclRelease(); hold(); sdaRelease(); hold(); }

namespace i2c {

void begin() {
    VPORTA.OUT &= ~(SDA_bm | SCL_bm);         // OUT stays 0 for the whole run
    VPORTA.DIR &= ~(SDA_bm | SCL_bm);
    PORTA.PIN6CTRL |= PORT_PULLUPEN_bm;
    PORTA.PIN7CTRL |= PORT_PULLUPEN_bm;
    _delay_ms(50);
    // If a warm reset left the panel mid-byte, clock it free.
    for (uint8_t i = 0; i < 9 && !sdaRead(); i++) { sclLow(); hold(); sclRelease(); hold(); }
    rawStop();
}

void beginTx(uint8_t a) { rawStart(); txAck = rawWrite(a << 1); }
void write(uint8_t b)   { if (txAck) txAck = rawWrite(b); }
bool endTx()            { rawStop(); return txAck; }
bool probe(uint8_t a)   { beginTx(a); return endTx(); }

}
#endif
