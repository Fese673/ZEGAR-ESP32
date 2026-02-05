#include "AudioBT.h"
#include "BluetoothA2DPSinkQueued.h"
#include "esp_bt.h"

static BluetoothA2DPSinkQueued* a2dp = nullptr;
static volatile bool connected = false;

// Callback połączenia
static void connection_state_callback(esp_a2d_connection_state_t state, void*) {
    connected = (state == ESP_A2D_CONNECTION_STATE_CONNECTED);
}

void audioBT_init() {
    // KROK 1: Zwolnij BLE (oszczędność 50-70KB RAM)
    esp_bt_controller_mem_release(ESP_BT_MODE_BLE);
    
    // KROK 2: Utwórz A2DP sink (Queued = osobny ringbuffer + I2S task)
    if (a2dp == nullptr) {
        a2dp = new BluetoothA2DPSinkQueued();
    }
    
    // KROK 3: Konfiguracja I2S - zoptymalizowane DMA
    i2s_config_t i2s_config = {
        .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX),
        .sample_rate = 44100,
        .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
        .channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT,
        .communication_format = I2S_COMM_FORMAT_STAND_I2S,
        .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
        .dma_buf_count = 12,               // 12 buforów - lepsza granularność
        .dma_buf_len = 128,                // 128 samples - mniej latency (~2.9ms/buf)
        .use_apll = true,                  // APLL = precyzyjny zegar audio (0 RAM)
        .tx_desc_auto_clear = true         // auto-clear przy underflow (cisza zamiast szumu)
    };
    
    // KROK 4: Konfiguracja pinów PCM5102
    i2s_pin_config_t pin_config = {
        .bck_io_num = 33,
        .ws_io_num = 32,
        .data_out_num = 14,
        .data_in_num = I2S_PIN_NO_CHANGE
    };
    
    // KROK 5: Zastosuj konfiguracje
    a2dp->set_i2s_config(i2s_config);
    a2dp->set_pin_config(pin_config);
    
    // KROK 6: Ringbuffer 16KB - bufor między BT a I2S (~0.18s audio)
    a2dp->set_i2s_ringbuffer_size(16 * 1024);       // 16KB - lekki bufor
    a2dp->set_i2s_ringbuffer_prefetch_percent(40);   // 40% (~6.4KB) start szybki
    a2dp->set_i2s_stack_size(2048);                  // domyślny stos I2S task
    
    // KROK 7: FreeRTOS - I2S task na Core 0, wysoki priorytet
    a2dp->set_task_core(0);
    a2dp->set_task_priority(configMAX_PRIORITIES - 2);
    
    // KROK 8: Callback połączenia
    a2dp->set_on_connection_state_changed(connection_state_callback);
    
    // KROK 9: Start
    a2dp->start("ESP32_AUDIO");
}

void audioBT_deinit() {
    if (a2dp != nullptr) {
        a2dp->end(true);
        delete a2dp;
        a2dp = nullptr;
    }
    connected = false;
}

bool audioBT_isConnected() {
    return connected;
}