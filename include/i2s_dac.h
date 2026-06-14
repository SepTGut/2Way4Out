/**
 * @file i2s_dac.h
 * @brief Public API for initialising the I2S peripherals that drive
 *        the two CS4344 DACs.
 *
 * The ESP32 has two independent I2S peripherals (I2S_NUM_0 and I2S_NUM_1).
 * Each one drives a separate CS4344 DAC:
 *   - I2S0 → high-band DAC (tweeter)
 *   - I2S1 → low-band DAC  (woofer)
 *
 * The CS4344 is a slave-mode 24-bit stereo DAC that accepts standard
 * I2S Philips format.  We use 16-bit samples (the DAC pads the lower
// * 8 bits with zeros) to match the A2DP source format and save memory.
 */

#pragma once

#include <driver/i2s.h>
#include "dsp_config.h"

/**
 * @brief Initialise an I2S peripheral for CS4344 DAC output.
 *
 * Configures the I2S peripheral as a master transmitter with:
 *   - I2S Philips standard format (required by CS4344)
 *   - 16-bit mono channel format (stereo via I2S_CHANNEL_FMT_RIGHT_LEFT)
 *   - APLL clock source for low jitter
 *   - DMA in double-buffer mode for smooth playback
 *
 * After installing the driver and setting pin mappings, the DMA buffers
 * are cleared (filled with zeros) so the DAC outputs silence until real
 * audio data is written via i2s_write().
 *
 * @param port     I2S peripheral to configure (I2S_NUM_0 or I2S_NUM_1).
 * @param bck_pin  GPIO number for the bit clock (BCK) signal.
 * @param ws_pin   GPIO number for the word-select / LRCK signal.
 * @param data_pin GPIO number for the serial data (SDOUT) signal.
 * @return true  if the I2S driver was installed successfully.
 * @return false if driver installation failed (e.g. invalid pin or port).
 */
bool i2s_init(i2s_port_t port, int bck_pin, int ws_pin, int data_pin);
