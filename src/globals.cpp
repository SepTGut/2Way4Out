/**
 * @file globals.cpp
 * @brief Definitions for shared global state.
 */

#include "globals.h"

// ── Audio pipeline ──
RingbufHandle_t audio_rb = NULL;

// ── DSP parameter protection ──
SemaphoreHandle_t dsp_params_mutex = NULL;
dsp_params_t current_params = dsp_params_default;

// ── Bluetooth state ──
volatile bt_conn_state_t bt_state = BT_DISCONNECTED;

// ── Smoothed parameters (DSP task writes, compressor/limiter/EQ read) ──
dsp_params_t smoothed_params = dsp_params_default;

// ── Crossover filter state ──
float filt_state[4] = {0.0f, 0.0f, 0.0f, 0.0f};

// ── Audio metrics ──
volatile float global_rms_level = 0.0f;

// ── EQ coefficient + state cache ──
float eq_coeffs[EQ_MAX_BANDS][5] = {};
float eq_state[EQ_MAX_BANDS][2][2] = {};

// ── Compressor state ──
float comp_env[2][2] = {};
float comp_gain[2][2] = {{1.0f, 1.0f}, {1.0f, 1.0f}};

// ── Limiter state ──
float lim_gain[2][2] = {{1.0f, 1.0f}, {1.0f, 1.0f}};

// ── Delay line state ──
int16_t *delay_buf_low_L  = NULL;
int16_t *delay_buf_low_R  = NULL;
int16_t *delay_buf_high_L = NULL;
int16_t *delay_buf_high_R = NULL;
uint16_t delay_write_idx[2] = {0, 0};

// ── WiFi / Web state ──
volatile bool    wifi_connected = false;
String           web_server_ip  = "";

// ── UI / control state ──
volatile int8_t  enc_count   = 0;
volatile bool    enc_pressed = false;
volatile bool    fs_flags[4] = {false, false, false, false};
uint8_t          adc_channels = 4;  // Default: single ADS1115
