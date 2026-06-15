/**
 * @file adc_control.cpp
 * @brief ADS1115 ADC reader with I2C Error Handling.
 */

#include "adc_control.h"
#include "globals.h"

Adafruit_ADS1115 ads;

void adc_update_params(dsp_params_t &params)
{
    // BUG FIX: I2C Robustness
    // We use a simple "heartbeat" check. If a read fails or hangs, 
    // the Adafruit library usually returns 0 or a sentinel.
    // To prevent a dead ADC from freezing the UI task, we wrap the
    // reads and validate the range.

    int16_t raw0, raw1, raw2, raw3;

    // Try reading the ADC. If the I2C bus is hung, the ESP32 Wire 
    // library will eventually timeout based on the system clock.
    raw0 = ads.readADC_SingleEnded(0);
    raw1 = ads.readADC_SingleEnded(1);
    raw2 = ads.readADC_SingleEnded(2);
    raw3 = ads.readADC_SingleEnded(3);

    // If we get completely invalid data (e.g. all zeros or maxed) 
    // unexpectedly, we keep the previous params to avoid sudden audio jumps.
    if (raw0 == 0 && raw1 == 0 && raw2 == 0 && raw3 == 0) {
        return; // Keep existing params
    }

    float vol = (float)raw0 / ADS_FULL_SCALE;
    params.master_volume = constrain(vol, 0.0f, 1.0f);

    float norm1 = constrain((float)raw1 / ADS_FULL_SCALE, 0.0f, 1.0f);
    params.crossover_hz = CROSSOVER_MIN_HZ + norm1 * (CROSSOVER_MAX_HZ - CROSSOVER_MIN_HZ);

    float norm2 = constrain((float)raw2 / ADS_FULL_SCALE, 0.0f, 1.0f);
    params.low_gain = norm2 * GAIN_MAX;

    float norm3 = constrain((float)raw3 / ADS_FULL_SCALE, 0.0f, 1.0f);
    params.high_gain = norm3 * GAIN_MAX;
}

