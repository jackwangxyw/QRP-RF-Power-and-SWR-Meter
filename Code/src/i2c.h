// ---------------------------------------------------------------------------
//  i2c.h - one API, two backends, picked by I2C_USE_HARDWARE in config.h.
//
//  Rev 2 needs the software path: hardware TWI0 sits on PB0/PB1 (unconnected)
//  and its alternate mapping is PA1/PA2, already used by the detectors.
//
//  Transaction shaped (beginTx / write / endTx) so it maps onto Wire without
//  any state trickery.
// ---------------------------------------------------------------------------
#pragma once
#include "compat.h"

// Bytes per data transaction. The software backend has no buffer and could
// stream the whole page, but Wire's TX buffer is small, so both are chunked
// to keep one code path. The SSD1306 keeps its write pointer between
// transactions, so a page can be split freely.
#define I2C_TX_CHUNK 16

namespace i2c {
void begin();
void beginTx(uint8_t addr7);
void write(uint8_t b);
bool endTx();                 // true if every byte was acknowledged
bool probe(uint8_t addr7);
}
