#pragma once

#include <driver/i2s.h>
#include "dsp_config.h"
#include "globals.h"

// Compute first-order IIR low-pass coefficients
void compute_coeffs(float cutoff_hz, float sample_rate, float *a0, float *b1);

// FreeRTOS task: read ring buffer → crossover → write to both DACs
void dsp_task(void *param);
