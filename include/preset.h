/**
 * @file preset.h
 * @brief Public API for preset storage and recall.
 *
 * Presets are stored in the ESP32's NVS (Non-Volatile Storage) flash.
 * Each preset contains a complete dsp_params_t snapshot plus a name.
 *
 * Up to NUM_PRESETS (8) presets can be stored.  Preset 0 is the default
 * and is loaded on boot if NVS storage is available.
 */

#pragma once

#include "dsp_config.h"

/**
 * @brief Initialise the NVS storage subsystem.
 *
 * Must be called once during setup() before any save/load operations.
 * Also loads preset 0 into current_params if a valid preset exists.
 *
 * @return true if NVS initialised successfully.
 */
bool preset_init();

/**
 * @brief Save the current DSP parameters to a preset slot.
 *
 * @param index  Preset slot index (0 … NUM_PRESETS-1).
 * @return true if saved successfully.
 */
bool preset_save(uint8_t index);

/**
 * @brief Load a preset into the current DSP parameters.
 *
 * The loaded parameters are written to current_params under mutex
 * protection, so the DSP task will pick them up on the next batch.
 *
 * @param index  Preset slot index (0 … NUM_PRESETS-1).
 * @return true if loaded successfully.
 */
bool preset_load(uint8_t index);

/**
 * @brief Get the name of a preset slot.
 *
 * @param index  Preset slot index.
 * @param name   Output buffer (at least 12 bytes).
 * @return true if the preset exists and the name was retrieved.
 */
bool preset_get_name(uint8_t index, char *name);

/**
 * @brief Set the name of a preset slot without changing parameters.
 *
 * Useful for renaming presets via the UI.
 *
 * @param index  Preset slot index.
 * @param name   New name (up to 11 characters + null terminator).
 * @return true if saved successfully.
 */
bool preset_set_name(uint8_t index, const char *name);
