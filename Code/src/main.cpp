// ---------------------------------------------------------------------------
//  QRP RF Power and SWR Meter - Rev 2   |   N6CH
//
//  ATtiny3224. Reads the forward and reflected detectors, converts to power
//  with a quartic calibration curve, shows power, SWR and battery on a
//  128x64 SSD1306.
//
//    short press  ->  watts / dBm  (remembered in EEPROM across power cycles)
//    long press   ->  calibration mode (raw ADC counts)
//
//  Dims, blanks and finally sleeps when there is no RF; returning RF wakes it.
//  See the power management block in config.h.
//
//  Everything tunable is in config.h; everything positional is in ui.cpp.
// ---------------------------------------------------------------------------
#include <Arduino.h>
#include <avr/sleep.h>
#include <util/delay.h>
#include <math.h>

#include "config.h"
#include "ssd1306.h"
#include "ui.h"
#include "meter.h"      // pure logic, unit-tested via `pio test -e test`

#if SAVE_UNITS
  #include <EEPROM.h>
#endif

// Sleep is only correct if millis() survives standby, which needs the RTC as
// the millis source (see build_flags). If something else claims it, fail here
// rather than ship a meter whose clock stops the moment it dozes off.
#if ENABLE_SLEEP && MILLIS_TYPE != MILLIS_RTC
  #error "ENABLE_SLEEP needs -DMILLIS_USE_TIMERRTC; a TC-based millis stops in standby"
#endif

enum Screen : uint8_t { SCREEN_METER, SCREEN_CAL };

static Screen  screen   = SCREEN_METER;
static bool    dbmMode  = false;
static bool    haveOled = false;

static float fwdEnv, revEnv;          // fast envelope, drives bar + peak marker
static float fwdAvg, revAvg;          // smoothed, drives the numerals and SWR
static float peakCounts;              // highest fwd since the last frame
static float battV;
static bool  fwdErr, revErr;          // last conversion on each channel faulted

static uint32_t tSample, tFrame, tFlash;
static bool     flashOn = true;

static PeakHold     peak;
static PowerManager power;
static PowerState   pstate = PWR_ACTIVE;

// ---------------------------------------------------------------------------
//  ADC
// ---------------------------------------------------------------------------
//  analogReadEnh() with extra bits uses the ADC's own accumulator, so 16
//  samples cost one conversion sequence rather than a software loop. The
//  result is scaled back to the 12-bit domain (keeping the fraction) so the
//  calibration polynomial still takes plain 12-bit counts.
//  The core signals faults as a negative return. Reporting that as 0 counts
//  would be indistinguishable from a genuinely dead input, which is exactly the
//  reading cal mode exists to be trusted on - so latch it and let ui::cal say so.
static float readCounts(uint8_t pin, bool &err) {
    int32_t v = analogReadEnh(pin, ADC_RESOLUTION_BITS + ADC_OVERSAMPLE_BITS);
    err = (v < 0);
    if (err) return 0.0f;
    return (float)v / (float)(1 << ADC_OVERSAMPLE_BITS);
}

//  Two filters off the same conversion: a fast envelope for the bar and peak
//  marker, and a slower symmetric one for the numerals. dtMs is the real gap
//  since the last call, which is well above SAMPLE_INTERVAL_MS whenever a frame
//  is going out - hence time-driven rather than per-sample coefficients.
//
//  Below the noise floor the readout snaps rather than coasting down through its
//  time constant. The detector RC is 0.2 ms, so no signal really does mean no
//  signal, and crawling to zero over half a second just looks broken.
static void sample(uint16_t dtMs) {
    const float fwdRawC = readCounts(PIN_VFWD, fwdErr);
    const float revRawC = readCounts(PIN_VREV, revErr);

    fwdEnv = envelopeMs(fwdEnv, fwdRawC, dtMs, ENVELOPE_TAU_MS);
    revEnv = envelopeMs(revEnv, revRawC, dtMs, ENVELOPE_TAU_MS);

    fwdAvg = (fwdRawC < (float)NOISE_FLOOR_COUNTS)
           ? fwdRawC : readoutMs(fwdAvg, fwdRawC, dtMs, READOUT_TAU_MS);
    revAvg = (revRawC < (float)NOISE_FLOOR_COUNTS)
           ? revRawC : readoutMs(revAvg, revRawC, dtMs, READOUT_TAU_MS);

    // Peak capture runs at the sample rate so an SSB syllable is never missed;
    // the hold and fall happen once a frame, where the timing is intuitive.
    if (fwdEnv > peakCounts) peakCounts = fwdEnv;

    int32_t vd = analogReadEnh(ADC_VDDDIV10, ADC_RESOLUTION_BITS);
    if (vd > 0) {
        float v = (float)vd * (ADC_REF_VOLTS / (ADC_MAX_COUNTS + 1)) * 10.0f;
        battV = (battV == 0.0f) ? v : battV * 0.9f + v * 0.1f;
    }
}

// ---------------------------------------------------------------------------
//  Saved settings
// ---------------------------------------------------------------------------
//  Only the unit choice is persisted. The screen deliberately is not: cal mode
//  is a diagnostic, and booting into it because that is where you left off
//  would look like a fault.
static void loadUnits() {
#if SAVE_UNITS
    dbmMode = (EEPROM.read(EE_ADDR_UNITS) == 1);   // 0xFF (erased) -> watts
#endif
}

//  ~11 ms of stalled CPU when the byte actually changes, which is once per
//  button press and invisible next to the frame it triggers.
static void saveUnits() {
#if SAVE_UNITS
    EEPROM.update(EE_ADDR_UNITS, dbmMode ? 1 : 0);
#endif
}

// ---------------------------------------------------------------------------
//  Button
// ---------------------------------------------------------------------------
static Button button;

static void serviceButton() {
    switch (button.update(digitalRead(PIN_BUTTON) == LOW, millis())) {
    case BTN_SHORT:
        if (screen == SCREEN_METER) { dbmMode = !dbmMode; saveUnits(); tFrame = 0; }
        break;
    case BTN_LONG:
        screen = (screen == SCREEN_METER) ? SCREEN_CAL : SCREEN_METER;
        tFrame = 0;
        break;
    default:
        break;
    }
}

// ---------------------------------------------------------------------------
//  Power management
// ---------------------------------------------------------------------------
//  Nothing watches the detectors while the CPU sleeps - PA1/PA2 are ADC-only
//  and their input buffers are off - so "wake on RF" is really the PIT poking
//  us every STANDBY_PIT_PERIOD to take one reading. Under 1% duty cycle.
//
//  The PIT is free to use: megaTinyCore's RTC millis runs off RTC_CNT_vect and
//  sets RUNSTDBY, so the clock keeps counting through standby and only the
//  errata workaround touches PITCTRLA - and that is gated to non-2-series.
static void pitInit() {
    while (RTC.PITSTATUS & RTC_CTRLBUSY_bm) {}
    RTC.PITINTCTRL = RTC_PI_bm;
    RTC.PITCTRLA   = STANDBY_PIT_PERIOD | RTC_PITEN_bm;
}

ISR(RTC_PIT_vect) { RTC.PITINTFLAGS = RTC_PI_bm; }   // wake only, no work

// Sleep until the next PIT tick, then take one cheap reading. No oversampling
// and no envelope - this is a threshold test, not a measurement.
static bool standbyTick() {
    ADC0.CTRLA &= ~ADC_ENABLE_bm;
    set_sleep_mode(SLEEP_MODE_STANDBY);
    sleep_enable();
    sleep_cpu();
    sleep_disable();

    ADC0.CTRLA |= ADC_ENABLE_bm;
    _delay_us(ADC_WAKE_SETTLE_US);               // 2.5 V reference settling

    int32_t v = analogRead(PIN_VFWD);
    if (v < WAKE_COUNTS) return false;

    fwdEnv = (float)v;                           // seed so the first frame is live
    return true;
}

static void applyState(PowerState from, PowerState to) {
    if (to == from) return;
    switch (to) {
    case PWR_ACTIVE:
        if (from >= PWR_BLANK) {
            oled::displayOn(true);
            delay(OLED_WAKE_MS);                 // charge pump
            peak.init(millis());                 // never show a stale peak
            peakCounts = fwdEnv;
        }
        oled::setContrast(OLED_CONTRAST_ACTIVE);
        tFrame = 0;                              // redraw at once
        break;
    case PWR_DIM:
        oled::setContrast(OLED_CONTRAST_DIM);
        break;
    case PWR_BLANK:
    case PWR_STANDBY:
        if (from < PWR_BLANK) oled::displayOn(false);
        break;
    }
}

// ---------------------------------------------------------------------------
void setup() {
    // megaTinyCore 2.6.11 ORs the RTC prescaler group value into CTRLA
    // unshifted (timers.h, _RTC_PRESCALE_VALUE), leaving PRESCALER at DIV1 - so
    // the RTC ticks 32x fast and every millis() timeout fires 32x early.
    while (RTC.STATUS & RTC_CTRLABUSY_bm) {}
    RTC.CTRLA = RTC_RUNSTDBY_bm | RTC_PRESCALER_DIV32_gc | RTC_RTCEN_bm;

    loadUnits();        // before the first frame, so it never shows watts first

    button.init();
    pinMode(PIN_BUTTON, INPUT_PULLUP);
    pinMode(PIN_VFWD, INPUT);
    pinMode(PIN_VREV, INPUT);
    // Kill the digital input buffers on the analog pins: they burn current and
    // couple noise into the conversion when the input sits near mid-rail.
    PORTA.PIN1CTRL = (PORTA.PIN1CTRL & ~PORT_ISC_gm) | PORT_ISC_INPUT_DISABLE_gc;
    PORTA.PIN2CTRL = (PORTA.PIN2CTRL & ~PORT_ISC_gm) | PORT_ISC_INPUT_DISABLE_gc;

    analogReference(ADC_REFERENCE);
    analogReadResolution(ADC_RESOLUTION_BITS);   // the core defaults to 10
    analogSampleDuration(ADC_SAMPLE_DURATION);   // 5k source impedance

    haveOled = oled::begin();
    if (!haveOled) { delay(250); haveOled = oled::begin(); }

    fwdEnv = fwdAvg = readCounts(PIN_VFWD, fwdErr);
    revEnv = revAvg = readCounts(PIN_VREV, revErr);
    peakCounts = fwdEnv;
    sample(SAMPLE_INTERVAL_MS);

#if ENABLE_SLEEP
    pitInit();
#endif
    peak.init(millis());
    power.init(millis());

#if SPLASH_MS > 0
    if (haveOled) { ui::splash(battV); delay(SPLASH_MS); }
#endif
    power.poke(millis());                        // don't count the splash as idle
}

void loop() {
    uint32_t t = millis();

#if ENABLE_SLEEP
    // Asleep: the only thing that runs is the PIT poll. The button is not a
    // wake source by design, so it is not read here at all.
    if (pstate == PWR_STANDBY) {
        if (standbyTick()) power.poke(millis());
        else               return;
        t = millis();
    }
#endif

    // Ignore the button while blanked: it cannot bring the screen back (RF
    // only), so acting on it would silently toggle units behind a dark panel.
    if (pstate <= PWR_DIM) serviceButton();

    uint32_t dt = t - tSample;
    if (dt >= SAMPLE_INTERVAL_MS) {
        tSample = t;
        sample((uint16_t)(dt > 1000 ? 1000 : dt));   // cap after a sleep or splash
    }

    if (fwdEnv >= (float)WAKE_COUNTS) power.poke(t);
    else if (pstate <= PWR_DIM && button.stable) power.poke(t);

#if ENABLE_SLEEP
    PowerState want = power.update(t);
#else
    PowerState want = PWR_ACTIVE;
#endif
    if (haveOled) applyState(pstate, want);
    pstate = want;

    if (!haveOled || pstate >= PWR_BLANK) return;

    // Numerals, SWR and the warnings all come off the smoothed value so they
    // agree with each other; only the bar and the peak marker run off fwdEnv.
    MeterState s;
    s.dbm       = countsToDbm(fwdAvg);       // the calibrated quantity
    s.watts     = dbmToWatts(s.dbm);
    s.wattsBar  = countsToWatts(fwdEnv);
    s.swr       = computeSwr(s.watts, countsToWatts(revAvg));
    s.battVolts = battV;
    s.fwdRaw    = (uint16_t)(fwdAvg + 0.5f);
    s.revRaw    = (uint16_t)(revAvg + 0.5f);
    s.fwdErr    = fwdErr;
    s.revErr    = revErr;
    s.dbmMode   = dbmMode;

    bool warnPwr = s.watts > POWER_WARN_THRESHOLD;
    bool warnSwr = !isnan(s.swr) && (isinf(s.swr) || s.swr > SWR_WARN_THRESHOLD);
    if (screen == SCREEN_CAL) warnPwr = warnSwr = false;

    if (warnPwr || warnSwr) {
        // both blink on the same tick when both are over
        if (t - tFlash >= WARN_FLASH_MS) { tFlash = t; flashOn = !flashOn; }
    } else {
        flashOn = true;
    }
    s.blankPwr = warnPwr && !flashOn;
    s.blankSwr = warnSwr && !flashOn;

    if (t - tFrame >= FRAME_INTERVAL_MS) {
        tFrame = t;
        // Feed the frame's highest sample in, then start the next window from
        // the live envelope so the peak cannot creep upward on its own.
        s.peakFrac = peak.update(countsToWatts(peakCounts) / PWR_BAR_FULL_SCALE, t);
        peakCounts = fwdEnv;
        if (screen == SCREEN_CAL) ui::cal(s); else ui::meter(s);
    }
}
