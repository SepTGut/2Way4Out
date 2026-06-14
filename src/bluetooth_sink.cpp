#include "bluetooth_sink.h"
#include "globals.h"
#include <Arduino.h>

BluetoothA2DPSink a2dp_sink;
const char *BT_DEVICE_NAME = "esp32DSP";

// ── Callbacks ──────────────────────────────────────────
static void a2dp_data_cb(const uint8_t *data, uint32_t len)
{
    if (audio_rb == NULL) return;
    xRingbufferSend(audio_rb, data, len, 0);
}

static void bt_conn_state_cb(bool connected)
{
    bt_state = connected ? BT_CONNECTED : BT_DISCONNECTED;
}

static void bt_audio_state_cb(esp_a2d_audio_state_t state, void *)
{
    if (state == ESP_A2D_AUDIO_STATE_STARTED) {
        bt_state = BT_PLAYING;
    } else if (state == ESP_A2D_AUDIO_STATE_STOPPED ||
               state == ESP_A2D_AUDIO_STATE_REMOTE_SUSPEND) {
        if (bt_state == BT_PLAYING) bt_state = BT_CONNECTED;
    }
}

// ── Public API ─────────────────────────────────────────
void bt_init()
{
    a2dp_sink.set_stream_reader(a2dp_data_cb, false);
    a2dp_sink.set_avrc_connection_state_callback(bt_conn_state_cb);
    a2dp_sink.set_on_audio_state_changed(bt_audio_state_cb);
    a2dp_sink.start(BT_DEVICE_NAME);
    Serial.printf("[OK] A2DP sink started as '%s'\n", BT_DEVICE_NAME);
}
