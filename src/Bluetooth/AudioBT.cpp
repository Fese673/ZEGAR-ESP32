#include "AudioBT.h"
#include "BluetoothA2DPSinkQueued.h"
#include "esp_bt.h"
#include "esp_bt_main.h"
#include "esp_log.h"
#include "BoardPins.h"

#if A2DP_I2S_AUDIOTOOLS
static audio_tools::I2SStream s_audioStream;
#endif

static BluetoothA2DPSinkQueued* a2dp = nullptr;
static volatile bool connected = false;

// Callback połączenia
static void connection_state_callback(esp_a2d_connection_state_t state, void*) {
    connected = (state == ESP_A2D_CONNECTION_STATE_CONNECTED);
}

bool audioBT_init() {
    // Reduce BT stack log churn in runtime audio mode to minimize UART-side jitter.
    esp_log_level_set("BT_AV", ESP_LOG_WARN);
    esp_log_level_set("BT_API", ESP_LOG_WARN);
    esp_log_level_set("RCCT", ESP_LOG_WARN);

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
    i2sConfig.pin_bck = BoardPins::kBtI2sBclk;
    i2sConfig.pin_ws = BoardPins::kBtI2sWs;
    i2sConfig.pin_data = BoardPins::kBtI2sData;

    if (!s_audioStream.begin(i2sConfig)) {
        Serial.println("[BT] Failed to initialize AudioTools I2S output");
        return false;
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
        .bck_io_num = BoardPins::kBtI2sBclk,
        .ws_io_num = BoardPins::kBtI2sWs,
        .data_out_num = BoardPins::kBtI2sData,
        .data_in_num = I2S_PIN_NO_CHANGE
    };

    a2dp->set_i2s_config(i2s_config);
    a2dp->set_pin_config(pin_config);
#else
    Serial.println("[BT] No supported audio backend available");
    return false;
#endif

    // KROK 4: Ringbuffer + I2S queue tuned for stable playback under mixed system load.
    a2dp->set_i2s_ringbuffer_size(24 * 1024);
    a2dp->set_i2s_ringbuffer_prefetch_percent(50);
    a2dp->set_i2s_stack_size(3072);
    a2dp->set_i2s_write_size_upto(240 * 8);
    a2dp->set_i2s_ticks(4);
    
    // KROK 5: FreeRTOS isolation profile.
    // - Core 0: BT app + I2S queue task
    // - Core 1: UI/WiFi/MQTT path
    a2dp->set_task_core(0);
    a2dp->set_task_priority(configMAX_PRIORITIES - 4);
    a2dp->set_event_queue_size(32);
    a2dp->set_event_stack_size(4096);
    a2dp->set_i2s_task_priority(configMAX_PRIORITIES - 2);
    
    // KROK 6: Callback połączenia
    a2dp->set_on_connection_state_changed(connection_state_callback);
    
    // KROK 7: Start
    a2dp->start("ESP32-BASS");

    const bool controllerReady = esp_bt_controller_get_status() == ESP_BT_CONTROLLER_STATUS_ENABLED;
    const bool bluedroidReady = esp_bluedroid_get_status() == ESP_BLUEDROID_STATUS_ENABLED;
    if (!controllerReady || !bluedroidReady) {
        Serial.printf("[BT] start failed: controller=%d bluedroid=%d\n",
                      (int)esp_bt_controller_get_status(),
                      (int)esp_bluedroid_get_status());
        audioBT_deinit();
        return false;
    }

    return true;
}

void audioBT_deinit() {
    if (a2dp != nullptr) {
        const esp_bt_controller_status_t controllerStatus = esp_bt_controller_get_status();
        const esp_bluedroid_status_t bluedroidStatus = esp_bluedroid_get_status();
        const bool stackWasInitialized =
            (controllerStatus != ESP_BT_CONTROLLER_STATUS_IDLE) ||
            (bluedroidStatus != ESP_BLUEDROID_STATUS_UNINITIALIZED);

        if (stackWasInitialized) {
            // Keep CLASSIC BT memory allocated so BT can be re-started without reboot.
            a2dp->end(false);
        }

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