/**
 * @file globals.cpp
 * @brief Definitions of shared global state declared in include/globals.h.
 *
 * This is the ONE translation unit that actually allocates storage for
 * the global variables.  Every other module includes globals.h and
 * references them via the `extern` declarations.
 *
 * Initialization values:
 *   - audio_rb:        nullptr (created in setup() via xRingbufferCreate)
 *   - dsp_params_mutex: nullptr (created in setup() via xSemaphoreCreateMutex)
 *   - current_params:  dsp_params_default (safe fallback before pots are read)
 *   - bt_state:        BT_DISCONNECTED (no phone connected at boot)
 *   - filt_state[]:    {0, 0, 0, 0} (silence — no filter memory yet)
 */

#include "globals.h"

/// Audio ring buffer handle.  Created in setup(), used by A2DP callback + DSP task.
RingbufHandle_t audio_rb = nullptr;

/// Mutex protecting current_params.  Created in setup().
SemaphoreHandle_t dsp_params_mutex = nullptr;

/// Current DSP parameters.  Safe default until the first ADC read.
dsp_params_t current_params = dsp_params_default;

/// Bluetooth connection state.  Starts as disconnected.
volatile bt_conn_state_t bt_state = BT_DISCONNECTED;

/// Crossover filter state memory.  All zeros = no prior audio history.
float filt_state[4] = {0};
