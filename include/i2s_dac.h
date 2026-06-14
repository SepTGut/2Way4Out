#pragma once

#include <driver/i2s.h>
#include "dsp_config.h"

// Initialise an I2S port for CS4344 DAC output
bool i2s_init(i2s_port_t port, int bck_pin, int ws_pin, int data_pin);
