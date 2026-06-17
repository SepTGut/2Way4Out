/**
 * @file adc_control.cpp
 * @brief ADS1115 ADC reader with I2C error handling, encoder decoding,
 *        and footswitch ISR management.
 *
 * Encoder decoding uses a simple state-machine approach on the two
 * quadrature signals.  The ISR fires on both edges of channel A and
 * reads the state of channel B to determine direction.
 *
 * Footswitches use GPIO ISRs with a simple timestamp-based debounce.
 */

#include "adc_control.h"
#include "globals.h"
#include "preset.h"
#include "pins.h"
#include <Arduino.h>

// ─────────────────────────────────────────────────────────────
// ADS1115 instances
// ─────────────────────────────────────────────────────────────
Adafruit_ADS1115 ads1;  // Address 0x48
Adafruit_ADS1115 ads2;  // Address 0x49

// ─────────────────────────────────────────────────────────────
// Encoder state machine table
// ─────────────────────────────────────────────────────────────
// The 4-bit state is (old_A << 3) | (old_B << 2) | (new_A << 1) | new_B.
// +1 = clockwise, -1 = counter-clockwise, 0 = no change / bounce.
static const int8_t enc_table[16] = {
    0,  +1, -1,  0,   // 00 → 00, 01, 10, 11
   -1,   0,  0, +1,   // 01 → 00, 01, 10, 11
   +1,   0,  0, -1,   // 10 → 00, 01, 10, 11
    0,  -1, +1,  0,   // 11 → 00, 01, 10, 11
};
static uint8_t enc_last_state = 0;

// ─────────────────────────────────────────────────────────────
// Footswitch debounce
// ─────────────────────────────────────────────────────────────
static volatile uint32_t fs_last_irq[4] = {0, 0, 0, 0};
static const uint32_t FS_DEBOUNCE_MS = 150;

// Pin mapping for footswitches
static const int fs_pins[4] = {
    PIN_FS_PRESET_NEXT,
    PIN_FS_PRESET_PREV,
    PIN_FS_MUTE,
    PIN_FS_BYPASS,
};

// ─────────────────────────────────────────────────────────────
// Encoder ISR
// ─────────────────────────────────────────────────────────────
static void IRAM_ATTR enc_isr(void)
{
    uint8_t new_a = digitalRead(PIN_ENC_A);
    uint8_t new_b = digitalRead(PIN_ENC_B);
    uint8_t state = (enc_last_state << 2) | (new_a << 1) | new_b;
    int8_t delta = enc_table[state & 0x0F];
    enc_count += delta;
    enc_last_state = state & 0x03;  // Keep only the new state bits
}

// ─────────────────────────────────────────────────────────────
// Footswitch ISRs (one per pin, but we use a generic handler)
// ─────────────────────────────────────────────────────────────
static void IRAM_ATTR fs_isr_generic(void *arg)
{
    uint32_t idx = (uint32_t)arg;
    uint32_t now = millis();
    if (now - fs_last_irq[idx] > FS_DEBOUNCE_MS) {
        fs_flags[idx] = true;
        fs_last_irq[idx] = now;
    }
}

// We need separate ISR functions for each pin because ESP32 ISRs
// don't take arguments in the attachInterrupt model.
static void IRAM_ATTR fs_isr_0(void) { fs_isr_generic((void *)0); }
static void IRAM_ATTR fs_isr_1(void) { fs_isr_generic((void *)1); }
static void IRAM_ATTR fs_isr_2(void) { fs_isr_generic((void *)2); }
static void IRAM_ATTR fs_isr_3(void) { fs_isr_generic((void *)3); }

// ─────────────────────────────────────────────────────────────
// Encoder button ISR
// ─────────────────────────────────────────────────────────────
static volatile uint32_t enc_btn_last_irq = 0;

static void IRAM_ATTR enc_btn_isr(void)
{
    uint32_t now = millis();
    if (now - enc_btn_last_irq > FS_DEBOUNCE_MS) {
        enc_pressed = true;
        enc_btn_last_irq = now;
    }
}

// ─────────────────────────────────────────────────────────────
// Public API
// ─────────────────────────────────────────────────────────────

void adc_init()
{
    // ── ADS1115 #1 (0x48) — primary controls ──
    if (!ads1.begin(ADS1115_ADDRESS)) {
        Serial.println("[WARN] ADS1115 #1 (0x48) not found! Check wiring.");
    } else {
        ads1.setGain(GAIN_ONE);
        Serial.println("[OK] ADS1115 #1 (0x48) initialised");
    }

    // ── ADS1115 #2 (0x49) — EQ / expression ──
    if (!ads2.begin(ADS1115_ADDRESS_2)) {
        Serial.println("[WARN] ADS1115 #2 (0x49) not found! EQ pots disabled.");
        adc_channels = 4;
    } else {
        ads2.setGain(GAIN_ONE);
        adc_channels = 8;
        Serial.println("[OK] ADS1115 #2 (0x49) initialised");
    }

    // ── Encoder ──
    pinMode(PIN_ENC_A, INPUT_PULLUP);
    pinMode(PIN_ENC_B, INPUT_PULLUP);
    pinMode(PIN_ENC_BTN, INPUT_PULLUP);

    // Read initial state
    enc_last_state = (digitalRead(PIN_ENC_A) << 1) | digitalRead(PIN_ENC_B);

    // Attach ISR to encoder channel A (both edges)
    attachInterrupt(digitalPinToInterrupt(PIN_ENC_A), enc_isr, CHANGE);
    attachInterrupt(digitalPinToInterrupt(PIN_ENC_B), enc_isr, CHANGE);

    // Attach ISR to encoder button (falling edge — active low)
    attachInterrupt(digitalPinToInterrupt(PIN_ENC_BTN), enc_btn_isr, FALLING);

    Serial.println("[OK] Encoder initialised (GPIO 34/35/17)");

    // ── Footswitches ──
    pinMode(PIN_FS_PRESET_NEXT, INPUT_PULLUP);
    pinMode(PIN_FS_PRESET_PREV, INPUT_PULLUP);
    pinMode(PIN_FS_MUTE, INPUT_PULLUP);
    pinMode(PIN_FS_BYPASS, INPUT_PULLUP);

    attachInterrupt(digitalPinToInterrupt(PIN_FS_PRESET_NEXT), fs_isr_0, FALLING);
    attachInterrupt(digitalPinToInterrupt(PIN_FS_PRESET_PREV), fs_isr_1, FALLING);
    attachInterrupt(digitalPinToInterrupt(PIN_FS_MUTE),       fs_isr_2, FALLING);
    attachInterrupt(digitalPinToInterrupt(PIN_FS_BYPASS),     fs_isr_3, FALLING);

    Serial.println("[OK] Footswitches initialised (GPIO 32/33/36/39)");
}

int16_t adc_read_channel(Adafruit_ADS1115 &adc, uint8_t channel, int16_t prev)
{
    if (channel > 3) return prev;
    int16_t raw = adc.readADC_SingleEnded(channel);
    // Basic validation: if the read returns exactly 0, it might be an I2C error
    // (a real 0V reading is unlikely with a pot between 3.3V and GND)
    if (raw == 0) return prev;
    return raw;
}

float adc_map_log_freq(float norm, float f_min, float f_max)
{
    // Logarithmic mapping: perceptually even spacing for frequency knobs
    if (norm <= 0.0f) return f_min;
    if (norm >= 1.0f) return f_max;
    return f_min * powf(f_max / f_min, norm);
}

void adc_update_params(dsp_params_t &params)
{
    // ── Read ADS1115 #1 (0x48) — primary controls ──
    int16_t raw0 = adc_read_channel(ads1, 0, 0);
    int16_t raw1 = adc_read_channel(ads1, 1, 0);
    int16_t raw2 = adc_read_channel(ads1, 2, 0);
    int16_t raw3 = adc_read_channel(ads1, 3, 0);

    // If all channels read 0, the ADC is probably disconnected — skip update
    if (raw0 == 0 && raw1 == 0 && raw2 == 0 && raw3 == 0) {
        // Keep existing params — don't overwrite with zeros
    } else {
        float vol = (float)raw0 / ADS_FULL_SCALE;
        params.master_volume = constrain(vol, 0.0f, 1.0f);

        float norm1 = constrain((float)raw1 / ADS_FULL_SCALE, 0.0f, 1.0f);
        params.crossover_hz = CROSSOVER_MIN_HZ + norm1 * (CROSSOVER_MAX_HZ - CROSSOVER_MIN_HZ);

        float norm2 = constrain((float)raw2 / ADS_FULL_SCALE, 0.0f, 1.0f);
        params.low_gain = norm2 * GAIN_MAX;

        float norm3 = constrain((float)raw3 / ADS_FULL_SCALE, 0.0f, 1.0f);
        params.high_gain = norm3 * GAIN_MAX;
    }

    // ── Read ADS1115 #2 (0x49) — EQ controls ──
    if (adc_channels >= 8) {
        int16_t raw4 = adc_read_channel(ads2, 0, 0);
        int16_t raw5 = adc_read_channel(ads2, 1, 0);
        int16_t raw6 = adc_read_channel(ads2, 2, 0);
        int16_t raw7 = adc_read_channel(ads2, 3, 0);

        if (!(raw4 == 0 && raw5 == 0 && raw6 == 0 && raw7 == 0)) {
            // EQ Band 0 frequency (logarithmic: 20 Hz – 20 kHz)
            float norm4 = constrain((float)raw4 / ADS_FULL_SCALE, 0.0f, 1.0f);
            params.eq_bands[0].freq_hz = adc_map_log_freq(norm4, 20.0f, 20000.0f);
            params.eq_bands[0].enabled = true;

            // EQ Band 0 gain (-12 to +12 dB)
            float norm5 = constrain((float)raw5 / ADS_FULL_SCALE, 0.0f, 1.0f);
            params.eq_bands[0].gain_db = (norm5 * 24.0f) - 12.0f;

            // EQ Band 1 frequency (logarithmic: 20 Hz – 20 kHz)
            float norm6 = constrain((float)raw6 / ADS_FULL_SCALE, 0.0f, 1.0f);
            params.eq_bands[1].freq_hz = adc_map_log_freq(norm6, 20.0f, 20000.0f);
            params.eq_bands[1].enabled = true;

            // EQ Band 1 gain (-12 to +12 dB)
            float norm7 = constrain((float)raw7 / ADS_FULL_SCALE, 0.0f, 1.0f);
            params.eq_bands[1].gain_db = (norm7 * 24.0f) - 12.0f;
        }
    }

    // ── Process encoder input ──
    int8_t enc_delta = enc_count;
    if (enc_delta != 0) {
        // Atomically clear the counter
        enc_count = 0;

        // Encoder adjusts the parameter selected by the active page
        // Page 0: master volume, Page 1: EQ freq, Page 2: EQ gain, etc.
        switch (params.active_page) {
            case 0: {  // Master volume
                float step = (float)enc_delta * 0.01f;  // 1% per click
                params.master_volume = constrain(params.master_volume + step, 0.0f, 1.0f);
                break;
            }
            case 1: {  // EQ band 0 frequency
                float step = (float)enc_delta * 50.0f;  // 50 Hz per click
                params.eq_bands[0].freq_hz = constrain(params.eq_bands[0].freq_hz + step, 20.0f, 20000.0f);
                params.eq_bands[0].enabled = true;
                break;
            }
            case 2: {  // EQ band 0 gain
                float step = (float)enc_delta * 0.5f;  // 0.5 dB per click
                params.eq_bands[0].gain_db = constrain(params.eq_bands[0].gain_db + step, -12.0f, 12.0f);
                break;
            }
            case 3: {  // Crossover frequency
                float step = (float)enc_delta * 100.0f;  // 100 Hz per click
                params.crossover_hz = constrain(params.crossover_hz + step, CROSSOVER_MIN_HZ, CROSSOVER_MAX_HZ);
                break;
            }
            default:
                break;
        }
    }

    // ── Process encoder button (page cycling) ──
    if (enc_pressed) {
        enc_pressed = false;
        params.active_page = (params.active_page + 1) % 5;  // 5 pages: 0-4
    }

    // ── Process footswitch flags ──
    for (uint8_t i = 0; i < 4; i++) {
        if (fs_flags[i]) {
            fs_flags[i] = false;
            switch (i) {
                case 0:  // Preset next
                    params.active_preset = (params.active_preset + 1) % NUM_PRESETS;
                    // TODO: load_preset(params.active_preset);
                    break;
                case 1:  // Preset previous
                    params.active_preset = (params.active_preset + NUM_PRESETS - 1) % NUM_PRESETS;
                    // TODO: load_preset(params.active_preset);
                    break;
                case 2:  // Mute toggle
                    params.mute = !params.mute;
                    break;
                case 3:  // Bypass toggle
                    params.bypass = !params.bypass;
                    break;
            }
        }
    }
}
