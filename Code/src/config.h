// ---------------------------------------------------------------------------
//  QRP RF Power and SWR Meter - Rev 2
//  Every tunable lives here. To calibrate, only CAL_A..CAL_E need touching.
// ---------------------------------------------------------------------------
#pragma once

// Everything below the pin map is plain arithmetic, so this header also
// compiles on the host for `pio test -e test`. Only the Arduino-specific
// bits are guarded.
#ifdef ARDUINO
  #include <Arduino.h>
  #include <avr/pgmspace.h>
#else
  #include <stdint.h>
#endif

#define FW_VERSION "v1.0"

// ---------------------------------------------------------------------------
//  Pin map
// ---------------------------------------------------------------------------
#ifdef ARDUINO
//  T1's winding phasing, not the layout, decides which sense port carries the
//  forward wave - and it lands opposite to the D1/D2 assignment on this board,
//  so PA2 is forward with the radio on J1. Swapped here rather than rewinding a
//  working coupler; the two detector chains are identical, so nothing else cares.
#define PIN_VFWD    PIN_PA2     // pin 12, via R7/R8 divider
#define PIN_VREV    PIN_PA1     // pin 11, via R3/R4 divider
#define PIN_BUTTON  PIN_PA3     // pin 13, SW1 to GND
// PA6 = SDA (J4.3), PA7 = SCL (J4.4) - see i2c.cpp
#endif

// ---------------------------------------------------------------------------
//  Display bus
// ---------------------------------------------------------------------------
//  Rev 2 must use software I2C: hardware TWI0 is on PB0/PB1 (unconnected) and
//  its alternate mapping is PA1/PA2, already taken by the detectors. The
//  hardware path is kept for a future board rev that routes the OLED to PB0/PB1.
#define I2C_USE_HARDWARE  0

//  Half a bit period. ~400 kHz is the practical ceiling
#define I2C_HALF_US       1

#define OLED_ADDRESS      0x3C  // 0x3D is probed automatically as a fallback

//  Panel brightness, 0x00..0xFF (SSD1306 contrast register). Drives segment
//  current, so it is most of what the display costs. DIM is what the meter
//  falls back to after SLEEP_DIM_MS with no RF.
//
//  Measured on hardware: this register saves real current but is not a usable
//  brightness control. 0x00 is unreadable and everything from 0x04 to 0x10
//  looks alike, so DIM is a power state, not a visible one. Don't bisect it
//  again - BLANK turning the panel off is the change you can actually see.
#define OLED_CONTRAST_ACTIVE  0xCF
#define OLED_CONTRAST_DIM     0x10

//  Charge pump settling after 0xAF before the panel is readable. Datasheet
//  asks for 100 ms; most modules are quicker, so this is worth trimming once
//  there is real hardware to watch.
#define OLED_WAKE_MS          100

// ---------------------------------------------------------------------------
//  Appearance
// ---------------------------------------------------------------------------
//  >>>>>>>>>>>>>>>>>>>>  NUMERAL FONT - PICK ONE  <<<<<<<<<<<<<<<<<<<<
//
//    BIG_FONT_CUSTOM     20 px tall, 2 px* monoline, 16 px cell, "25.0" = 60 px
//    BIG_FONT_MICHROMA   16 px tall, synthetic bold +1, 21 px cell, "25.0" = 70 px
//
//  Change the one line below and rebuild. Nothing else needs touching -
//  ui.cpp reads the numeral size back from the font and re-centres the
//  whole readout block to suit.
//
//  * the shipped custom set is drawn at a 3 px stroke; regenerate from
//    tools/make_digits.py to change it.

#define BIG_FONT_MICHROMA 1               // selector values - do not edit
#define BIG_FONT_CUSTOM   2

#define BIG_FONT   BIG_FONT_CUSTOM        // <-- switch here

#if BIG_FONT != BIG_FONT_MICHROMA && BIG_FONT != BIG_FONT_CUSTOM
  #error "BIG_FONT must be BIG_FONT_MICHROMA or BIG_FONT_CUSTOM"
#endif

//  >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>

// Inset every element by this many pixels so nothing sits on the extreme
// edge, guarding against case/bezel alignment slop. Content is NOT shrunk -
// the rows come out of the gap between the power bar and the SWR band, and
// the SWR bar moves up off row 63. 0 restores the original layout.
#define UI_MARGIN         1

#define SHOW_POWER_BAR    1     // continuous 0..25 W bar under the power reading
#define SPLASH_MS         3000  //amount of time that splash screen stays

// ---------------------------------------------------------------------------
//  Calibration
// ---------------------------------------------------------------------------
//      dBm = A*L^4 + B*L^3 + C*L^2 + D*L + E     where L = log10(raw count)
//
//  Watts is then derived: W = 10^((dBm - 30) / 10).
//
//  Two deliberate choices here, both worth understanding before re-fitting.
//
//  1. Fit dBm, not watts. Least squares on watts weights absolute error, so a
//     0.3 W miss at 25 W outweighs every point below 1 W put together and the
//     fit simply ignores the bottom of the range. dBm is a relative measure,
//     so every decade gets equal weight.
//
//  2. Fit against log10(counts), not counts. A quartic in raw counts cannot
//     follow a log curve over a 200:1 span - measured against the previous
//     watts-domain curve it misses by up to 5.9 dB. In log10(counts) the same
//     quartic lands within 0.11 dB worst case, 0.013 dB above 0.25 W.
//
//  One curve serves both channels - the detector chains are identical
//  (same diode, R1||R2 == R5||R6 == 50R, R3/R4 == R7/R8).
//
//  To calibrate: record dBm against raw count, then in Desmos run
//      y1 ~ a(log(x1))^4 + b(log(x1))^3 + c(log(x1))^2 + d*log(x1) + e
//  with x1 = counts and y1 = dBm. Paste a..e into A..E below.
//
//  NOTE: the fit is against raw counts, so these constants are tied to the
//  2.5 V reference and the /2 divider. Change either and they are wrong.
//
//  The shipped values are a refit of the previous watts-domain curve into this
//  form - same response, new representation. They are still borrowed, not
//  measured, and want replacing with real bench data.
// ---------------------------------------------------------------------------
#define CAL_A  -3.558687e-01f
#define CAL_B   3.941184e+00f
#define CAL_C  -1.359802e+01f
#define CAL_D   2.977850e+01f
#define CAL_E  -1.072073e+01f

//  Trim for a consistent error. In a dBm-native curve a fixed percentage error
//  is a fixed dB offset, so this is simply added: +0.5 makes every reading
//  0.5 dB higher. (Replaces the old CAL_MULTIPLIER, which was a linear factor
//  on watts and no longer fits the arithmetic.)
#define CAL_OFFSET_DB      0.0f
#define NOISE_FLOOR_COUNTS 20     // below this, no signal

// ---------------------------------------------------------------------------
//  ADC
// ---------------------------------------------------------------------------
//  No regulator on this board - VDD is the cell - so the reference must be
//  internal or every reading drifts as the battery drains. 2.5 V puts 25 W at
//  ~3800 of 4095 counts. 2.048 V would clip at ~19 W; 4.096 V needs VDD > 4.3 V.
//  megaTinyCore spells this one INTERNAL2V5, not INTERNAL2V500.
#ifdef ARDUINO
#define ADC_REFERENCE       INTERNAL2V5
#endif
#define ADC_REF_VOLTS       2.5f
#define ADC_MAX_COUNTS      4095
#define ADC_RESOLUTION_BITS 12    // the core defaults to 10 - must be set

//  Hardware oversampling, in extra bits: 2 -> 16 samples -> 4x less noise.
//  Worth it in the 2-10 W range, where the display shows two decimals and one
//  LSB is only ~1.7x finer than the last digit, so raw counts visibly flicker.
//  Result is scaled back to the 12-bit domain, so calibration is unaffected.
//  3 bits = 64 samples, halving the noise versus 16. Costs ~1.1 ms per channel
//  against ~0.3 ms, which buys the shorter READOUT_TAU_MS below. 4 bits needs
//  256 samples / 4.6 ms per channel and overruns the sample tick.
#define ADC_OVERSAMPLE_BITS 3
#define ADC_SAMPLE_DURATION 32    // source impedance is R3||R4 = 5k

// ---------------------------------------------------------------------------
//  Rates
// ---------------------------------------------------------------------------
#define SAMPLE_INTERVAL_MS  5     // 200 Hz detector sampling
#define FRAME_INTERVAL_MS   33    // ~30 fps; only changed pages are pushed
//  Both filters below are driven by ELAPSED TIME, not by sample count. A frame
//  push is tens of ms of bit-banged I2C, so the tick above cannot actually hold
//  200 Hz - a per-sample coefficient stretched these constants by 5-10x and made
//  them vary with display load. These are real milliseconds.
//
//  ENVELOPE: instant attack, exponential decay. Feeds the bar and peak marker,
//  so it wants to be quick enough to show individual SSB syllables.
//
//  READOUT: symmetric, feeds the numerals and SWR. Averaging ~tau/tick samples
//  is what steadies the last decimal, which is worth only 1-2 ADC counts in the
//  5-10 W range. Longer = steadier but laggier.
//  A steady digit and a fast fall pull against each other, so the noise is
//  bought down at the ADC (see ADC_OVERSAMPLE_BITS) rather than in here. That
//  is what lets READOUT_TAU_MS be short enough to fall like the Rev 1 unit.
#define ENVELOPE_TAU_MS     40    // bar and peak marker
#define READOUT_TAU_MS      40    // numerals and SWR

// ---------------------------------------------------------------------------
//  Warnings - the offending numeral blinks, its label and unit stay put
// ---------------------------------------------------------------------------
#define SWR_WARN_THRESHOLD   2.5f
#define POWER_WARN_THRESHOLD 25.0f
#define WARN_FLASH_MS        350   // half period of the blink
#define SWR_MIN_POWER        0.05f // below this SWR is meaningless

// ---------------------------------------------------------------------------
//  Bars
// ---------------------------------------------------------------------------
#define PWR_BAR_FULL_SCALE   25.0f  // linear, continuous

// ---------------------------------------------------------------------------
//  Peak marker - a short block on the power bar at the highest recent reading
// ---------------------------------------------------------------------------
//  Holds for PEAK_HOLD_MS after each new peak, then falls back to the live
//  reading at PEAK_FALL_WPS. On voice every syllable re-arms the hold, so the
//  marker stays pinned at PEP for as long as you are talking and only starts
//  falling once you stop - which is what makes it readable when ENVELOPE_DECAY
//  is set fast enough to be twitchy.
#define SHOW_PEAK_MARKER  1
#define PEAK_HOLD_MS      2000
#define PEAK_FALL_WPS     8.0f   // watts per second; 25 W -> 0 in ~3 s
#define PEAK_MARKER_W     2      // marker width in px
#define PEAK_GAP          2      // min clear px between bar and marker, or hide

// Above this the readout shows LIM instead of a number: the ADC saturates
// around 28.7 W with the 2.5 V reference, so anything past here is guesswork.
// Applies to dBm too, since the test is on watts.
#define POWER_LIMIT_WATTS    28.0f
#define SWR_BAR_MAX          10.0f  // logarithmic, with divider notches

// ---------------------------------------------------------------------------
//  Button
// ---------------------------------------------------------------------------
#define BUTTON_DEBOUNCE_MS   25
#define BUTTON_LONG_PRESS_MS 700

// ---------------------------------------------------------------------------
//  Battery - read from the ADC's internal VDD/10 channel, no extra parts
// ---------------------------------------------------------------------------
#define BATT_LOW_VOLTS       3.30f

// ---------------------------------------------------------------------------
//  Power management
// ---------------------------------------------------------------------------
//  Four states, each trading a little more wake latency for a lot less
//  current. Idle means no RF; RF returning is the ONLY thing that wakes the
//  meter from BLANK or STANDBY.
//
//    ACTIVE           full brightness, 200 Hz sampling      ~25 mA
//    DIM      2 min   OLED_CONTRAST_DIM, still sampling     ~15 mA   ~5 ms wake
//    BLANK   10 min   panel off, MCU still sampling         ~4 mA  ~100 ms wake
//    STANDBY  1 hour  panel off, MCU asleep, PIT polling    ~30 uA ~350 ms wake
//
//  Sampling stays at the full rate in BLANK: the MCU is awake and burning
//  milliamps either way, so slowing the ADC would buy nothing and only add
//  latency. Sleeping between samples is what STANDBY is for.
//
//  BLANK deliberately runs for most of an hour: the 50 minutes between it and
//  the STANDBY threshold cost ~3.3 mAh of a 1000 mAh cell, and in exchange a
//  real operating session never reaches STANDBY at all - so wake stays at
//  ~100 ms throughout, and STANDBY becomes purely the forgot-to-switch-off
//  case where its ~350 ms does not matter.
#define ENABLE_SLEEP         1
#define SLEEP_DIM_MS         120000UL
#define SLEEP_BLANK_MS       600000UL
#define SLEEP_STANDBY_MS     3600000UL

//  Counts on VFWD that count as "RF present". Must clear NOISE_FLOOR_COUNTS
//  with margin or the meter will never settle; 40 counts is about 39 mW, well
//  under the ~0.25 W where readings become trustworthy.
//  Measured 8 Aug 2026 on the Rev 2 board: resting VFWD is 0 counts with
//  nothing connected, so 40 has the full margin and the meter sleeps reliably.
#define WAKE_COUNTS          40

//  How often STANDBY wakes to look for RF. The PIT divides 32.768 kHz by
//  powers of two only, so pick from the list:
//    CYC4096 = 125 ms   CYC8192 = 250 ms   CYC16384 = 500 ms   CYC32768 = 1 s
//  Faster is more responsive; the wake bursts dominate the average current, so
//  250 ms costs ~30 uA and 1 s would cost ~9 uA. Both are years on this cell.
#ifdef ARDUINO
#define STANDBY_PIT_PERIOD   RTC_PERIOD_CYC8192_gc
#endif

//  Settling time for the 2.5 V reference after the ADC is re-enabled on wake.
#define ADC_WAKE_SETTLE_US   50
