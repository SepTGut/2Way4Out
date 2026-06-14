/**
 * @file adc_control.cpp
 * @brief ADS1115 ADC reader — converts raw potentiometer values into
 *        DSP parameters.
 *
 * Called periodically by the UI task (every UI_REFRESH_MS = 200 ms).
 * Each call triggers 4 single-shot ADC conversions (one per channel),
 * normalises the raw values, and fills a dsp_params_t struct.
 *
 * The ADS1115 is configured at GAIN_ONE (±4.096V range, 0.125 mV/LSB)
 * which is set in setup() via ads.setGain(GAIN_ONE).
 */

#include "adc_control.h"
#include "globals.h"

// ── ADS1115 instance definition ──────────────────────────────
// Single chip on the I2C bus at address 0x48 (ADDR pin → GND).
Adafruit_ADS1115 ads;

void adc_update_params(dsp_params_t &params)
{
    // ── Channel 0: Master Volume ─────────────────────────────
    // Raw range: 0 … ADS_FULL_SCALE (≈26666)
    // Normalised: 0.0 (silent) … 1.0 (full volume)
    int16_t raw0 = ads.readADC_SingleEnded(0);
    float vol = (float)raw0 / ADS_FULL_SCALE;
    params.master_volume = constrain(vol, 0.0f, 1.0f);

    // ── Channel 1: Crossover Frequency ───────────────────────
    // Raw range: 0 … ADS_FULL_SCALE
    // Mapped linearly to CROSSOVER_MIN_HZ … CROSSOVER_MAX_HZ
    // (200 Hz … 4000 Hz).  A linear mapping is used because the
    // human ear perceives frequency logarithmically, but the pot
    // itself provides a linear voltage — the user can compensate
    // by adjusting the knob position.
    int16_t raw1 = ads.readADC_SingleEnded(1);
    float norm1 = constrain((float)raw1 / ADS_FULL_SCALE, 0.0f, 1.0f);
    params.crossover_hz = CROSSOVER_MIN_HZ
                        + norm1 * (CROSSOVER_MAX_HZ - CROSSOVER_MIN_HZ);

    // ── Channel 2: Low-Band Gain ─────────────────────────────
    // Raw range: 0 … ADS_FULL_SCALE
    // Mapped to 0.0 … GAIN_MAX (0.0 … 2.0)
    // Allows cutting or boosting the woofer output relative to the tweeter.
    int16_t raw2 = ads.readADC_SingleEnded(2);
    float norm2 = constrain((float)raw2 / ADS_FULL_SCALE, 0.0f, 1.0f);
    params.low_gain = norm2 * GAIN_MAX;

    // ── Channel 3: High-Band Gain ────────────────────────────
    // Same mapping as low-band gain.
    // Allows cutting or boosting the tweeter output relative to the woofer.
    int16_t raw3 = ads.readADC_SingleEnded(3);
    float norm3 = constrain((float)raw3 / ADS_FULL_SCALE, 0.0f, 1.0f);
    params.high_gain = norm3 * GAIN_MAX;
}
