#include "AudioBT.h"
#include "BluetoothA2DPSinkQueued.h"
#include "esp_bt.h"

#if A2DP_I2S_AUDIOTOOLS
static audio_tools::I2SStream s_audioStream;
#endif

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
    
    // KROK 3: Konfiguracja wyjścia audio.
#if A2DP_I2S_AUDIOTOOLS
    audio_tools::I2SConfig i2sConfig = s_audioStream.defaultConfig(audio_tools::TX_MODE);
    i2sConfig.sample_rate = 44100;
    i2sConfig.bits_per_sample = 16;
    i2sConfig.channels = 2;
    i2sConfig.is_master = true;
    i2sConfig.use_apll = true;
    i2sConfig.auto_clear = true;
    i2sConfig.buffer_count = 12;
    i2sConfig.buffer_size = 128;
    i2sConfig.pin_bck = 33;
    i2sConfig.pin_ws = 32;
    i2sConfig.pin_data = 14;

    if (!s_audioStream.begin(i2sConfig)) {
        Serial.println("[BT] Failed to initialize AudioTools I2S output");
        return;
    }

    a2dp->set_output(s_audioStream);
#elif A2DP_LEGACY_I2S_SUPPORT
    i2s_config_t i2s_config = {
        .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX),
        .sample_rate = 44100,
        .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
        .channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT,
        .communication_format = I2S_COMM_FORMAT_STAND_I2S,
        .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
        .dma_buf_count = 12,
        .dma_buf_len = 128,
        .use_apll = true,
        .tx_desc_auto_clear = true
    };

    i2s_pin_config_t pin_config = {
        .bck_io_num = 33,
        .ws_io_num = 32,
        .data_out_num = 14,
        .data_in_num = I2S_PIN_NO_CHANGE
    };

    a2dp->set_i2s_config(i2s_config);
    a2dp->set_pin_config(pin_config);
#else
    Serial.println("[BT] No supported audio backend available");
    return;
#endif

    // KROK 4: Ringbuffer 16KB - bufor między BT a I2S (~0.18s audio)
    a2dp->set_i2s_ringbuffer_size(16 * 1024);       // 16KB - lekki bufor
    a2dp->set_i2s_ringbuffer_prefetch_percent(40);   // 40% (~6.4KB) start szybki
    a2dp->set_i2s_stack_size(2048);                  // domyślny stos I2S task
    
    // KROK 5: FreeRTOS - I2S task na Core 0, wysoki priorytet
    a2dp->set_task_core(0);
    a2dp->set_i2s_task_priority(configMAX_PRIORITIES - 2);
    
    // KROK 6: Callback połączenia
    a2dp->set_on_connection_state_changed(connection_state_callback);
    
    // KROK 7: Start
    a2dp->start("ESP32_AUDIO");
}

void audioBT_deinit() {
    if (a2dp != nullptr) {
        a2dp->end(true);
        delete a2dp;
        a2dp = nullptr;
    }
#if A2DP_I2S_AUDIOTOOLS
    s_audioStream.end();
#endif
    connected = false;
}

bool audioBT_isConnected() {
    return connected;
}