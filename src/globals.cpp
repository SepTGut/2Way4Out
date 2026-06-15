#include "globals.h"

RingbufHandle_t audio_rb = NULL;
SemaphoreHandle_t dsp_params_mutex = NULL;
dsp_params_t current_params = dsp_params_default;
volatile bt_conn_state_t bt_state = BT_DISCONNECTED;
float filt_state[4] = {0.0f, 0.0f, 0.0f, 0.0f};

// Initialize RMS level to 0
volatile float global_rms_level = 0.0f;

