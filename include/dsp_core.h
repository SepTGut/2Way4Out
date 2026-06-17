/**
 * @file dsp_core.h
 * @brief Public API for the DSP crossover processing core.
 *
 * This module implements a 2-way active crossover using a first-order
 * IIR low-pass filter (6 dB/octave).
 */

#pragma once

#include <driver/i2s.h>
#include "dsp_config.h"
#include "globals.h"

/**
 * @brief Compute first-order IIR low-pass filter coefficients.
 */
void compute_coeffs(float cutoff_hz, float sample_rate, float *a0, float *b1);

/**
 * @brief FreeRTOS task function for DSP audio processing.
 */
void dsp_task(void *param);
