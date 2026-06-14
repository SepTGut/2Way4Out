#include "adc_control.h"
#include "globals.h"

Adafruit_ADS1115 ads;

void adc_update_params(dsp_params_t &params)
{
    // ── Master volume (ADS channel 0) ──
    int16_t raw0 = ads.readADC_SingleEnded(0);
    float vol = (float)raw0 / ADS_FULL_SCALE;
    params.master_volume = constrain(vol, 0.0f, 1.0f);

    // ── Crossover frequency (ADS channel 1) ──
    int16_t raw1 = ads.readADC_SingleEnded(1);
    float norm1 = constrain((float)raw1 / ADS_FULL_SCALE, 0.0f, 1.0f);
    params.crossover_hz = CROSSOVER_MIN_HZ + norm1 * (CROSSOVER_MAX_HZ - CROSSOVER_MIN_HZ);

    // ── Low-band gain (ADS channel 2) ──
    int16_t raw2 = ads.readADC_SingleEnded(2);
    float norm2 = constrain((float)raw2 / ADS_FULL_SCALE, 0.0f, 1.0f);
    params.low_gain = norm2 * GAIN_MAX;

    // ── High-band gain (ADS channel 3) ──
    int16_t raw3 = ads.readADC_SingleEnded(3);
    float norm3 = constrain((float)raw3 / ADS_FULL_SCALE, 0.0f, 1.0f);
    params.high_gain = norm3 * GAIN_MAX;
}
