/**
 * @file ui_display.h
 * @brief Public API for the SSD1306 OLED status display.
 *
 * The 128×64 pixel I2C OLED shows multiple pages:
 *   Page 0 (Main):    Title, BT state, volume, crossover, gains, VU meter
 *   Page 1 (EQ 1-2):  EQ bands 0-1 frequency, gain, Q
 *   Page 2 (EQ 3-4):  EQ bands 2-3 frequency, gain, Q
 *   Page 3 (Dynamics): Compressor threshold/ratio, limiter threshold per band
 *   Page 4 (Delay):    Delay time per band, mute/bypass status
 *
 * The display is refreshed every UI_REFRESH_MS (200 ms) by the UI task.
 * All rendering is done via the Adafruit_SSD1306 + Adafruit_GFX libraries.
 */

#pragma once

#include <Adafruit_SSD1306.h>
#include "dsp_config.h"

// ─────────────────────────────────────────────────────────────
// Display dimensions
// ─────────────────────────────────────────────────────────────
// The SSD1306 is a 128×64 monochrome OLED.  We use the default
// I2C connection (no reset pin, hence -1 in the constructor).

/// Display width in pixels.
#define SCREEN_WIDTH  128

/// Display height in pixels.
#define SCREEN_HEIGHT 64

// ─────────────────────────────────────────────────────────────
// Display instance
// ─────────────────────────────────────────────────────────────
// Single global display object.  'extern' here; defined in ui_display.cpp.
extern Adafruit_SSD1306 display;

/**
 * @brief Initialise the SSD1306 OLED display.
 *
 * Powers up the display, clears the screen, and shows a brief
 * "ESP32-DSP / Starting..." message so the user knows the system
 * is booting.  Called once from setup() after I2C is initialised.
 *
 * If the display is not found on the I2C bus, a failure message
 * is printed to Serial but the system continues to operate
 * (audio still works, just no visual feedback).
 */
void ui_init();

/**
 * @brief Redraw the entire status screen.
 *
 * Renders the current page based on params.active_page.
 * Each page shows different DSP parameters.
 *
 * This function is called from the UI task every UI_REFRESH_MS.
 *
 * @param params  Current DSP parameters to display (read-only).
 */
void ui_update(const dsp_params_t &params);
