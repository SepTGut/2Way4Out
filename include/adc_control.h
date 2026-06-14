#pragma once

#include <Adafruit_ADS1X15.h>
#include "dsp_config.h"

// ADS1115 instance (initialised in adc_control.cpp)
extern Adafruit_ADS1115 ads;

// Read all four pots and populate params
void adc_update_params(dsp_params_t &params);
