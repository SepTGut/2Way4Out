#include "globals.h"

RingbufHandle_t audio_rb = nullptr;
SemaphoreHandle_t dsp_params_mutex = nullptr;
dsp_params_t current_params = dsp_params_default;
volatile bt_conn_state_t bt_state = BT_DISCONNECTED;
float filt_state[4] = {0};
