#pragma once

#include "BluetoothA2DPSink.h"
#include "dsp_config.h"

extern BluetoothA2DPSink a2dp_sink;
extern const char *BT_DEVICE_NAME;

// Start the A2DP-sink with ring-buffer + state callbacks
void bt_init();
