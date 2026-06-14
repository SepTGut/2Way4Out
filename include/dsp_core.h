/**
 * @file dsp_core.h
 * @brief Public API for the DSP crossover processing core.
 *
 * This module implements a 2-way active crossover using a first-order
 * IIR low-pass filter (6 dB/octave).  The high-pass signal is derived
 * by subtracting the low-pass output from the original signal.
 *
 * Signal flow per stereo sample:
 *
 *   raw_L ──┬──► [LPF] ──► low_L  ──► × low_gain  ──► × volume ──► low-band DAC (I2S1)
 *           │
 *           └──► raw_L - low_L ──► high_L ──► × high_gain ──► × volume ──► high-band DAC (I2S0)
 *
 *   (same for right channel)
 *
 * The DSP task runs on Core 1 at high priority, reading audio from the
 * ring buffer in batches of FRAMES_PER_BATCH (128 frames) and writing
 * the filtered outputs to both I2S peripherals.
 */

#pragma once

#include <driver/i2s.h>
#include "dsp_config.h"
#include "globals.h"

/**
 * @brief Compute first-order IIR low-pass filter coefficients.
 *
 * Uses the standard RC filter discretisation:
 *   alpha = dt / (RC + dt)
 *   where RC = 1 / (2π × cutoff_hz)  and  dt = 1 / sample_rate
 *
 * The filter difference equation is:
 *   y[n] = alpha × x[n] + (1 - alpha) × y[n-1]
 *
 * where alpha is stored in *a0 and (1 - alpha) in *b1.
 *
 * @param cutoff_hz  Desired cutoff frequency in Hz.
 *                   Clamped to [10 … 20000] to prevent instability.
 * @param sample_rate Sample rate in Hz (e.g. 44100).
 * @param a0 [out]   Pointer to store the feedforward coefficient (alpha).
 * @param b1 [out]   Pointer to store the feedback coefficient (1 - alpha).
 */
void compute_coeffs(float cutoff_hz, float sample_rate, float *a0, float *b1);

/**
 * @brief FreeRTOS task function for DSP audio processing.
 *
 * This is the high-priority audio processing task.  It:
 *   1. Allocates DMA-capable buffers for raw, low, and high audio.
 *   2. In an infinite loop:
 *      a. Reads a batch of audio from the ring buffer.
 *      b. Grabs the current DSP parameters (under mutex).
 *      c. Computes crossover filter coefficients from the crossover frequency.
 *      d. Applies the crossover filter to each stereo frame.
 *      e. Applies per-band gain and master volume.
 *      f. Clips to 16-bit range to prevent DAC overflow.
 *      g. Writes the low-band output to I2S1 (woofer DAC).
 *      h. Writes the high-band output to I2S0 (tweeter DAC).
 *
 * If no audio data is available from the ring buffer (e.g. Bluetooth
 * is connected but not streaming), silence (zeros) is written to both
 * DACs to keep them active and prevent popping.
 *
 * @param param Unused (required by FreeRTOS task signature).
 */
void dsp_task(void *param);
