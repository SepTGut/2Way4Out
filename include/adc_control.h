/**
 * @file adc_control.h
 * @brief Public API for reading the ADS1115 ADC(s) to obtain user-controlled
 *        DSP parameters from potentiometers, plus encoder and footswitch handling.
 *
 * Potentiometer wiring (ADS1115 #1, address 0x48, single-ended mode):
 *   CH0 (AIN0)  → Master volume knob
 *   CH1 (AIN1)  → Crossover frequency knob
 *   CH2 (AIN2)  → Low-band gain knob
 *   CH3 (AIN3)  → High-band gain knob
 *
 * Potentiometer wiring (ADS1115 #2, address 0x49, single-ended mode):
 *   CH0 (AIN0)  → EQ Band 0 frequency
 *   CH1 (AIN1)  → EQ Band 0 gain
 *   CH2 (AIN2)  → EQ Band 1 frequency
 *   CH3 (AIN3)  → Expression pedal input (or EQ Band 1 gain)
 *
 * The pots are wired as voltage dividers between 3.3V and GND, with the
 * wiper connected to the ADS1115 input.  At GAIN_ONE the ADC reads
 * 0 … +4.096V as raw values 0 … ~26666.
 *
 * Encoder: quadrature decoder on GPIO 34 (A) and GPIO 35 (B).
 * Footswitches: active-low momentary buttons on GPIO 32, 33, 36, 39.
 */

#pragma once

#include <Adafruit_ADS1X15.h>
#include "dsp_config.h"

// ─────────────────────────────────────────────────────────────
// ADS1115 instances
// ─────────────────────────────────────────────────────────────
// Two ADS1115 chips on the same I2C bus.
// 'extern' here; actual objects are defined in adc_control.cpp.
extern Adafruit_ADS1115 ads1;  // Address 0x48 — primary controls
extern Adafruit_ADS1115 ads2;  // Address 0x49 — EQ / expression

// ─────────────────────────────────────────────────────────────
// ADC + Encoder + Footswitch API
// ─────────────────────────────────────────────────────────────

/**
 * @brief Initialise both ADS1115 ADCs, encoder GPIO, and footswitch GPIO.
 *
 * Called once from setup() after I2C is initialised.
 * Sets up:
 *   - ADS1115 #1 at 0x48 (GAIN_ONE)
 *   - ADS1115 #2 at 0x49 (GAIN_ONE) — if present
 *   - Encoder pins (34, 35) as inputs with pull-ups + ISR
 *   - Encoder button pin (17) as input with pull-up
 *   - Footswitch pins (32, 33, 36, 39) as inputs with pull-ups + ISR
 */
void adc_init();

/**
 * @brief Read all potentiometer channels and populate a dsp_params_t struct.
 *
 * Reads from both ADS1115 chips (if present) and maps the raw ADC values
 * to DSP parameters:
 *
 * ADS1115 #1 (0x48):
 *   CH0 → master_volume (0.0 … 1.0)
 *   CH1 → crossover_hz  (CROSSOVER_MIN_HZ … CROSSOVER_MAX_HZ)
 *   CH2 → low_gain      (0.0 … GAIN_MAX)
 *   CH3 → high_gain     (0.0 … GAIN_MAX)
 *
 * ADS1115 #2 (0x49):
 *   CH0 → eq_bands[0].freq_hz (20 … 20000, logarithmic mapping)
 *   CH1 → eq_bands[0].gain_db (-12.0 … +12.0)
 *   CH2 → eq_bands[1].freq_hz (20 … 20000, logarithmic mapping)
 *   CH3 → eq_bands[1].gain_db (-12.0 … +12.0) [or expression pedal]
 *
 * If a channel reads all zeros (I2C error), the previous value is kept.
 *
 * @param params  Pointer to the dsp_params_t struct to populate.
 */
void adc_update_params(dsp_params_t &params);

/**
 * @brief Read a single ADC channel with error checking.
 *
 * @param adc     Reference to the ADS1115 instance.
 * @param channel Channel number (0-3).
 * @param prev    Previous valid value (used as fallback on error).
 * @return Raw ADC reading, or prev if the read failed.
 */
int16_t adc_read_channel(Adafruit_ADS1115 &adc, uint8_t channel, int16_t prev);

/**
 * @brief Map a normalised 0-1 ADC value to a logarithmic frequency range.
 *
 * Useful for EQ frequency and crossover knobs where perceptual spacing
 * matters more than linear.
 *
 * @param norm   Normalised ADC value (0.0 … 1.0).
 * @param f_min  Minimum frequency in Hz.
 * @param f_max  Maximum frequency in Hz.
 * @return Frequency in Hz.
 */
float adc_map_log_freq(float norm, float f_min, float f_max);
