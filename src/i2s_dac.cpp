#include "i2s_dac.h"
#include "pins.h"
#include <Arduino.h>

bool i2s_init(i2s_port_t port, int bck_pin, int ws_pin, int data_pin)
{
    i2s_config_t i2s_cfg = {
        .mode                   = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX),
        .sample_rate            = SAMPLE_RATE,
        .bits_per_sample        = I2S_BITS_PER_SAMPLE_16BIT,
        .channel_format         = I2S_CHANNEL_FMT_RIGHT_LEFT,
        .communication_format   = (i2s_comm_format_t)(I2S_COMM_FORMAT_STAND_I2S),
        .intr_alloc_flags       = ESP_INTR_FLAG_LEVEL1,
        .dma_buf_count          = DMA_BUF_COUNT,
        .dma_buf_len            = DMA_BUF_LEN,
        .use_apll               = true,
        .tx_desc_auto_clear     = true,
        .fixed_mclk             = 0,
        .mclk_multiple          = I2S_MCLK_MULTIPLE_256,
        .bits_per_chan          = I2S_BITS_PER_CHAN_16BIT,
    };

    esp_err_t err = i2s_driver_install(port, &i2s_cfg, 0, NULL);
    if (err != ESP_OK) {
        Serial.printf("I2S driver install failed on port %d: 0x%x\n", port, err);
        return false;
    }

    i2s_pin_config_t pin_cfg = {
        .bck_io_num   = bck_pin,
        .ws_io_num    = ws_pin,
        .data_out_num = data_pin,
        .data_in_num  = I2S_PIN_NO_CHANGE,
    };
    i2s_set_pin(port, &pin_cfg);
    i2s_zero_dma_buffer(port);

    Serial.printf("I2S%d initialised (BCK=%d, WS=%d, DATA=%d)\n",
                  port, bck_pin, ws_pin, data_pin);
    return true;
}
