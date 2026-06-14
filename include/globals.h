#pragma once

#include <Arduino.h>
#include <driver/i2s.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/ringbuf.h"
#include "dsp_config.h"

// ──────────────────────────────────────────────
// Shared global state (defined in globals.cpp)
// ──────────────────────────────────────────────

extern RingbufHandle_t audio_rb;
extern SemaphoreHandle_t dsp_params_mutex;
extern dsp_params_t current_params;
extern volatile bt_conn_state_t bt_state;
extern float filt_state[4];
