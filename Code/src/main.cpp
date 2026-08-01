// ---------------------------------------------------------------------------
//  QRP RF Power and SWR Meter - Rev 2   |   N6CH
//
//  ATtiny3224. Reads the forward and reflected detectors, converts to power
//  with a quartic calibration curve, shows power, SWR and battery on a
//  128x64 SSD1306.
//
//    short press  ->  watts / dBm
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

static float fwdEnv, revEnv;          // envelope-followed counts, 12-bit scale
static float peakCounts;              // highest fwd since the last frame
static float battV;

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
static float readCounts(uint8_t pin) {
    int32_t v = analogReadEnh(pin, ADC_RESOLUTION_BITS + ADC_OVERSAMPLE_BITS);
    if (v < 0) return 0.0f;                       // core signals faults negative
    return (float)v / (float)(1 << ADC_OVERSAMPLE_BITS);
}

static void sample() {
    fwdEnv = envelopeStep(fwdEnv, readCounts(PIN_VFWD), ENVELOPE_DECAY);
    revEnv = envelopeStep(revEnv, readCounts(PIN_VREV), ENVELOPE_DECAY);

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
//  Button
// ---------------------------------------------------------------------------
static Button button;

static void serviceButton() {
    switch (button.update(digitalRead(PIN_BUTTON) == LOW, millis())) {
    case BTN_SHORT:
        if (screen == SCREEN_METER) { dbmMode = !dbmMode; tFrame = 0; }
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

    fwdEnv = readCounts(PIN_VFWD);
    revEnv = readCounts(PIN_VREV);
    peakCounts = fwdEnv;
    sample();

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

    if (t - tSample >= SAMPLE_INTERVAL_MS) { tSample = t; sample(); }

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

    MeterState s;
    s.dbm       = countsToDbm(fwdEnv);       // the calibrated quantity
    s.watts     = dbmToWatts(s.dbm);
    s.swr       = computeSwr(s.watts, countsToWatts(revEnv));
    s.battVolts = battV;
    s.fwdRaw    = (uint16_t)(fwdEnv + 0.5f);
    s.revRaw    = (uint16_t)(revEnv + 0.5f);
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
