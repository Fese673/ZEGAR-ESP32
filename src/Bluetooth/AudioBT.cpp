#include "AudioBT.h"
#include "BluetoothA2DPSink.h"
#include "esp_bt.h"

static BluetoothA2DPSink* a2dp = nullptr;
static volatile bool connected = false;

// Callback połączenia
static void connection_state_callback(esp_a2d_connection_state_t state, void*) {
    connected = (state == ESP_A2D_CONNECTION_STATE_CONNECTED);
}

void audioBT_init() {
    // KROK 1: Zwolnij BLE (oszczędność 50-70KB RAM)
    esp_bt_controller_mem_release(ESP_BT_MODE_BLE);
    
    // KROK 2: Utwórz A2DP sink
    if (a2dp == nullptr) {
        a2dp = new BluetoothA2DPSink();
    }
    
    // KROK 3: Konfiguracja I2S z optymalizacją DMA
    // Uwaga: unikamy pinów enkodera (25,26)
    i2s_config_t i2s_config = {
        .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX),
        .sample_rate = 44100,
        .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
        .channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT,
        .communication_format = I2S_COMM_FORMAT_STAND_I2S,
        .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
        .dma_buf_count = 6,                // zmniejszone z 8 (-2KB)
        .dma_buf_len = 60,                 // zmniejszone z 64
        .use_apll = false,
        .tx_desc_auto_clear = true
    };
    
    // KROK 5: Konfiguracja pinów PCM5102
    // Wybieramy piny wolne: BCK=33, WS=32, DATA=22 (unikamy 25/26 enkodera)
    i2s_pin_config_t pin_config = {
        .bck_io_num = 33,
        .ws_io_num = 32,
        .data_out_num = 22,
        .data_in_num = I2S_PIN_NO_CHANGE
    };
    
    // KROK 6: Zastosuj konfiguracje
    a2dp->set_i2s_config(i2s_config);
    a2dp->set_pin_config(pin_config);
    
    // KROK 7: Callback połączenia
    a2dp->set_on_connection_state_changed(connection_state_callback);
    
    // KROK 8: Start
    a2dp->start("ESP32_AUDIO");
}

void audioBT_deinit() {
    if (a2dp != nullptr) {
        a2dp->end(true);     // zatrzymaj i zwolnij pamięć BT
        delete a2dp;
        a2dp = nullptr;
    }
    connected = false;
}

bool audioBT_isConnected() {
    return connected;
}