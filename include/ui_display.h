#pragma once

#include <Adafruit_SSD1306.h>
#include "dsp_config.h"

#define SCREEN_WIDTH  128
#define SCREEN_HEIGHT 64

// SSD1306 display instance (defined in ui_display.cpp)
extern Adafruit_SSD1306 display;

// Initialise the OLED
void ui_init();

// Redraw the full status screen
void ui_update(const dsp_params_t &params);
