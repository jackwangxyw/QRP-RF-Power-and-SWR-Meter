// ---------------------------------------------------------------------------
//  compat.h - the one place that knows whether we are on the AVR or the host.
//
//  Lets ui.cpp / fonts.cpp / ssd1306.cpp compile for the screen simulator
//  (see sim/) without littering them with #ifdefs. On the ATtiny this is just
//  Arduino.h plus pgmspace; on the host, flash reads become plain pointer
//  dereferences because there is no separate program memory.
// ---------------------------------------------------------------------------
#pragma once

#ifdef ARDUINO
  #include <Arduino.h>
  #include <avr/pgmspace.h>
#else
  #include <stdint.h>
  #include <stddef.h>
  #ifndef PROGMEM
    #define PROGMEM
  #endif
  #ifndef pgm_read_byte
    #define pgm_read_byte(addr) (*(const uint8_t *)(addr))
  #endif
#endif
