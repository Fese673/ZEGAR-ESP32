#include "AudioBT.h"
#include "BluetoothA2DPSink.h"

// Wskaźnik zamiast obiektu (NULL na starcie)
static BluetoothA2DPSink* a2dp = nullptr;
static volatile bool connected = false;

// Callback stanu połączenia
static void connection_state_callback(esp_a2d_connection_state_t state, void*) {
    if (state == ESP_A2D_CONNECTION_STATE_CONNECTED) {
        connected = true;
    } else if (state == ESP_A2D_CONNECTION_STATE_DISCONNECTED) {
        connected = false;
    }
}

void audioBT_init() {
    // Utwórz obiekt DOPIERO tutaj (w setup(), kiedy system jest gotowy)
    if (a2dp == nullptr) {
        a2dp = new BluetoothA2DPSink();
    }

    // Wyłącz I2S (kolizja pinów)
    a2dp->set_stream_reader([](const uint8_t *data, uint32_t len) {
        // Dane audio - na razie ignorujemy
    }, false);

    // Callback połączenia
    a2dp->set_on_connection_state_changed(connection_state_callback);

    // Start
    a2dp->start("ESP32_AUDIO");
}

bool audioBT_isConnected() {
    return connected;
}
