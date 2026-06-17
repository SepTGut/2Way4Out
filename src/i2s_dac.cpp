/**
 * @file i2s_dac.cpp
 * @brief I2S peripheral initialisation for the CS4344 DACs.
 *
 * This module handles the low-level ESP32 I2S driver setup.  It is called
 * from setup() once per DAC (I2S0 and I2S1).  Each call installs the
 * I2S driver, routes the signals to the correct GPIO pins via the GPIO
 * matrix, and silences the DMA buffers.
 *
 * The CS4344 DAC expects:
 *   - I2S Philips standard (data changes on BCK falling edge)
 *   - Left-justified 24-bit data (we send 16-bit, DAC pads zeros)
 *   - No MCLK — the CS4344 derives it internally from BCK
 */

#include "i2s_dac.h"
#include "pins.h"
#include <Arduino.h>

bool i2s_init(i2s_port_t port, int bck_pin, int ws_pin, int data_pin)
{
    // ── I2S peripheral configuration ──────────────────────────
    // Mode: master transmitter (ESP32 generates BCK and WS)
    // Format: I2S Philips standard (required by CS4344)
    // Channels: stereo (RIGHT_LEFT format = left channel first, then right)
    // DMA: double-buffered to prevent gaps in audio output
    i2s_config_t i2s_cfg = {
        .mode                   = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX),
        .sample_rate            = SAMPLE_RATE,            // 44.1 kHz from A2DP
        .bits_per_sample        = I2S_BITS_PER_SAMPLE_16BIT,
        .channel_format         = I2S_CHANNEL_FMT_RIGHT_LEFT,  // stereo
        .communication_format   = (i2s_comm_format_t)(I2S_COMM_FORMAT_STAND_I2S),  // Philips
        .intr_alloc_flags       = ESP_INTR_FLAG_LEVEL1,    // low-priority ISR
        .dma_buf_count          = DMA_BUF_COUNT,           // 8 DMA buffers
        .dma_buf_len            = DMA_BUF_LEN,             // 64 samples each
        .use_apll               = true,                    // low-jitter audio PLL
        .tx_desc_auto_clear     = true,                    // auto-clear DMA on underrun
        .fixed_mclk             = 0,                       // MCLK not used (CS4344 is slave)
        .mclk_multiple          = I2S_MCLK_MULTIPLE_256,   // must be set even if unused
        .bits_per_chan          = I2S_BITS_PER_CHAN_16BIT,
    };

    // Install the I2S driver.  This allocates DMA channels, configures
    // the I2S peripheral registers, and sets up interrupt handlers.
    esp_err_t err = i2s_driver_install(port, &i2s_cfg, 0, NULL);
    if (err != ESP_OK) {
        Serial.printf("I2S driver install failed on port %d: 0x%x\n", port, err);
        return false;
    }

    // ── Pin routing via GPIO matrix ──────────────────────────
    // The ESP32's GPIO matrix allows any GPIO to be used for I2S signals.
    // We route BCK, WS, and DATA_OUT to the pins defined in pins.h.
    // DATA_IN is not needed (transmit-only).
    i2s_pin_config_t pin_cfg = {
        .bck_io_num   = bck_pin,
        .ws_io_num    = ws_pin,
        .data_out_num = data_pin,
        .data_in_num  = I2S_PIN_NO_CHANGE,  // no input from DAC
    };
    i2s_set_pin(port, &pin_cfg);

    // Clear DMA buffers so the DAC receives silence (zero samples) until
    // the DSP task starts writing real audio data.  This prevents random
    // noise on the outputs during boot.
    i2s_zero_dma_buffer(port);

    Serial.printf("I2S%d initialised (BCK=%d, WS=%d, DATA=%d)\n",
                  port, bck_pin, ws_pin, data_pin);
    return true;
}
