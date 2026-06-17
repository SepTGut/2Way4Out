/**
 * @file preset.cpp
 * @brief Preset storage using ESP32 NVS (Non-Volatile Storage).
 *
 * Each preset is stored as a blob in NVS under the key "preset_N" where
 * N is the preset index (0-7).  A separate key "preset_cnt" tracks how
 * many presets have been saved.
 *
 * NVS is wear-leveled by the ESP32 flash layer, so frequent saves are
 * acceptable (rated for ~100k erase cycles per sector).
 */

#include "preset.h"
#include "globals.h"
#include <nvs.h>
#include <nvs_flash.h>
#include <Arduino.h>

static const char *NVS_NAMESPACE = "dsp";
static const char *KEY_COUNT     = "preset_cnt";
static nvs_handle_t nvs_hnd      = 0;
static bool nvs_ready            = false;

// ─────────────────────────────────────────────────────────────
// Public API
// ─────────────────────────────────────────────────────────────

bool preset_init()
{
    // Initialise NVS flash
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        // NVS partition was truncated — erase and retry
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    if (err != ESP_OK) {
        Serial.printf("[FAIL] NVS init failed: 0x%x\n", err);
        return false;
    }

    // Open namespace
    err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs_hnd);
    if (err != ESP_OK) {
        Serial.printf("[FAIL] NVS open failed: 0x%x\n", err);
        return false;
    }

    nvs_ready = true;
    Serial.println("[OK] NVS preset storage initialised");

    // Load preset 0 if it exists
    uint8_t count = 0;
    err = nvs_get_u8(nvs_hnd, KEY_COUNT, &count);
    if (err == ESP_OK && count > 0) {
        if (preset_load(0)) {
            Serial.println("[OK] Loaded preset 0 on boot");
        }
    }

    return true;
}

bool preset_save(uint8_t index)
{
    if (!nvs_ready || index >= NUM_PRESETS) return false;

    char key[16];
    snprintf(key, sizeof(key), "preset_%u", index);

    // Pack the preset: name (12 bytes) + dsp_params_t
    preset_t preset;
    preset.params = current_params;
    snprintf(preset.name, sizeof(preset.name), "Preset %u", index + 1);

    esp_err_t err = nvs_set_blob(nvs_hnd, key, &preset, sizeof(preset_t));
    if (err != ESP_OK) {
        Serial.printf("[FAIL] Preset save %u failed: 0x%x\n", index, err);
        return false;
    }

    // Update preset count
    uint8_t count = 0;
    nvs_get_u8(nvs_hnd, KEY_COUNT, &count);
    if (index >= count) {
        nvs_set_u8(nvs_hnd, KEY_COUNT, index + 1);
    }

    err = nvs_commit(nvs_hnd);
    if (err != ESP_OK) {
        Serial.printf("[FAIL] NVS commit failed: 0x%x\n", err);
        return false;
    }

    Serial.printf("[OK] Preset %u saved\n", index);
    return true;
}

bool preset_load(uint8_t index)
{
    if (!nvs_ready || index >= NUM_PRESETS) return false;

    char key[16];
    snprintf(key, sizeof(key), "preset_%u", index);

    preset_t preset;
    size_t length = sizeof(preset_t);
    esp_err_t err = nvs_get_blob(nvs_hnd, key, &preset, &length);
    if (err != ESP_OK) {
        Serial.printf("[WARN] Preset %u not found\n", index);
        return false;
    }

    // Write to current_params under mutex
    if (xSemaphoreTake(dsp_params_mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        current_params = preset.params;
        current_params.active_preset = index;
        xSemaphoreGive(dsp_params_mutex);
    }

    Serial.printf("[OK] Preset %u loaded: %s\n", index, preset.name);
    return true;
}

bool preset_get_name(uint8_t index, char *name)
{
    if (!nvs_ready || index >= NUM_PRESETS || name == NULL) return false;

    char key[16];
    snprintf(key, sizeof(key), "preset_%u", index);

    preset_t preset;
    size_t length = sizeof(preset_t);
    esp_err_t err = nvs_get_blob(nvs_hnd, key, &preset, &length);
    if (err != ESP_OK) return false;

    strncpy(name, preset.name, 12);
    name[11] = '\0';
    return true;
}

bool preset_set_name(uint8_t index, const char *name)
{
    if (!nvs_ready || index >= NUM_PRESETS || name == NULL) return false;

    char key[16];
    snprintf(key, sizeof(key), "preset_%u", index);

    // Load existing preset
    preset_t preset;
    size_t length = sizeof(preset_t);
    esp_err_t err = nvs_get_blob(nvs_hnd, key, &preset, &length);
    if (err != ESP_OK) return false;

    // Update name
    strncpy(preset.name, name, sizeof(preset.name) - 1);
    preset.name[sizeof(preset.name) - 1] = '\0';

    // Save back
    err = nvs_set_blob(nvs_hnd, key, &preset, sizeof(preset_t));
    if (err != ESP_OK) return false;

    return nvs_commit(nvs_hnd) == ESP_OK;
}
