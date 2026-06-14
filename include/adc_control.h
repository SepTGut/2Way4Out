/**
 * @file adc_control.h
 * @brief Public API for reading the ADS1115 ADC to obtain user-controlled
 *        DSP parameters from the 4 potentiometers.
 *
 * Potentiometer wiring (ADS1115 single-ended mode):
 *   CH0 (AIN0)  → Master volume knob
 *   CH1 (AIN1)  → Crossover frequency knob
 *   CH2 (AIN2)  → Low-band gain knob
 *   CH3 (AIN3)  → High-band gain knob
 *
 * The pots are wired as voltage dividers between 3.3V and GND, with the
 * wiper connected to the ADS1115 input.  At GAIN_ONE the ADC reads
 * 0 … +4.096V as raw values 0 … ~26666.
 */

#pragma once

#include <Adafruit_ADS1X15.h>
#include "dsp_config.h"

// ─────────────────────────────────────────────────────────────
// ADS1115 instance
// ─────────────────────────────────────────────────────────────
// A single ADS1115 chip handles all 4 potentiometer channels.
// 'extern' here; actual object is defined in adc_control.cpp.
extern Adafruit_ADS1115 ads;

/**
 * @brief Read all 4 potentiometers and populate a dsp_params_t struct.
 *
 * Each ADC channel is read in single-shot mode (conversion triggered
 * per read).  Raw values are normalised to engineering units:
 *   - Volume:      0.0 … 1.0 (linear)
 *   - Crossover:   CROSSOVER_MIN_HZ … CROSSOVER_MAX_HZ (logarithmic feel via knob)
 *   - Gains:       0.0 … GAIN_MAX (linear)
 *
 * constrain() clamps any out-of-range values that could result from
 * electrical noise or pot tolerance.
 *
 * @param params  Pointer to the dsp_params_t struct to populate.
 *                The caller is responsible for copying this into
 *                the shared current_params (under mutex) if needed.
 */
void adc_update_params(dsp_params_t &params);
