#include "AudioBT.h"
#include "BluetoothA2DPSinkQueued.h"
#include "esp_bt.h"
#include "esp_bt_main.h"
#include "esp_log.h"
#include "BoardPins.h"
#include "AppLog.h"
#include "RamTelemetry.h"

#include <atomic>

#if A2DP_I2S_AUDIOTOOLS
static audio_tools::I2SStream s_audioStream;
static bool s_audioStreamActive = false;
#endif

namespace {

constexpr char TAG[] = "BT";
constexpr char kSinkName[] = "ESP32-BASS";

constexpr int kSampleRateHz = 44100;
constexpr int kBitsPerSample = 16;
constexpr int kChannelCount = 2;
constexpr int kI2sBufferCount = 8;
constexpr int kI2sBufferSize = 256;
constexpr int kRingbufferSizeBytes = 12 * 1024;
constexpr int kRingbufferPrefetchPercent = 80;
constexpr int kI2sStackSizeBytes = 3072;
constexpr size_t kI2sWriteSizeUpto = 240 * 8;
constexpr int kI2sTicks = 10;
constexpr UBaseType_t kTaskCore = 0;
constexpr UBaseType_t kTaskPriority = configMAX_PRIORITIES - 3;
constexpr UBaseType_t kI2sTaskPriority = configMAX_PRIORITIES - 1;
constexpr UBaseType_t kEventQueueSize = 32;
constexpr UBaseType_t kEventStackSize = 4096;

static BluetoothA2DPSinkQueued s_a2dp;
static std::atomic<bool> s_connected{false};
static bool s_audioInitialized = false;

void connection_state_callback(esp_a2d_connection_state_t state, void*) {
    s_connected.store(state == ESP_A2D_CONNECTION_STATE_CONNECTED);
}

void reduceBtLogNoise() {
    // Keep library logs readable, but avoid the highest-volume categories during playback.
    esp_log_level_set("BT_AV", ESP_LOG_WARN);
    esp_log_level_set("BT_API", ESP_LOG_WARN);
    esp_log_level_set("RCCT", ESP_LOG_WARN);
}

bool isBtStackReady() {
    const bool controllerReady = esp_bt_controller_get_status() == ESP_BT_CONTROLLER_STATUS_ENABLED;
    const bool bluedroidReady = esp_bluedroid_get_status() == ESP_BLUEDROID_STATUS_ENABLED;
    return controllerReady && bluedroidReady;
}

#if A2DP_I2S_AUDIOTOOLS
bool configureAudioToolsOutput() {
    audio_tools::I2SConfig i2sConfig = s_audioStream.defaultConfig(audio_tools::TX_MODE);
    i2sConfig.sample_rate = kSampleRateHz;
    i2sConfig.bits_per_sample = kBitsPerSample;
    i2sConfig.channels = kChannelCount;
    i2sConfig.is_master = true;
    i2sConfig.use_apll = true;
    i2sConfig.auto_clear = true;
    i2sConfig.buffer_count = kI2sBufferCount;
    i2sConfig.buffer_size = kI2sBufferSize;
    i2sConfig.pin_bck = BoardPins::kBtI2sBclk;
    i2sConfig.pin_ws = BoardPins::kBtI2sWs;
    i2sConfig.pin_data = BoardPins::kBtI2sData;

    if (!s_audioStream.begin(i2sConfig)) {
        s_audioStream.end();
        s_audioStreamActive = false;
        return false;
    }

    s_audioStreamActive = true;
    return true;
}

void stopAudioToolsOutput() {
    if (!s_audioStreamActive) {
        return;
    }

    s_audioStream.end();
    s_audioStreamActive = false;
}
#endif

void configureSink() {
    s_a2dp.set_i2s_ringbuffer_size(kRingbufferSizeBytes);
    s_a2dp.set_i2s_ringbuffer_prefetch_percent(kRingbufferPrefetchPercent);
    s_a2dp.set_i2s_stack_size(kI2sStackSizeBytes);
    s_a2dp.set_i2s_write_size_upto(kI2sWriteSizeUpto);
    s_a2dp.set_i2s_ticks(kI2sTicks);

    // Keep the realtime path on Core 0 and leave the rest of the app on Core 1.
    s_a2dp.set_task_core(kTaskCore);
    s_a2dp.set_task_priority(kTaskPriority);
    s_a2dp.set_event_queue_size(kEventQueueSize);
    s_a2dp.set_event_stack_size(kEventStackSize);
    s_a2dp.set_i2s_task_priority(kI2sTaskPriority);
    s_a2dp.set_on_connection_state_changed(connection_state_callback);
}

void shutdownAudio(bool writeCheckpoint) {
    const esp_bt_controller_status_t controllerStatus = esp_bt_controller_get_status();
    const esp_bluedroid_status_t bluedroidStatus = esp_bluedroid_get_status();
    const bool stackWasInitialized =
        (controllerStatus != ESP_BT_CONTROLLER_STATUS_IDLE) ||
        (bluedroidStatus != ESP_BLUEDROID_STATUS_UNINITIALIZED);

    s_audioInitialized = false;
    s_connected.store(false);

    if (stackWasInitialized) {
        s_a2dp.end(false);
#if A2DP_I2S_AUDIOTOOLS
        s_audioStreamActive = false;
#endif
    } else {
#if A2DP_I2S_AUDIOTOOLS
        stopAudioToolsOutput();
#endif
    }

    if (writeCheckpoint) {
        RAM_CHECKPOINT("AUDIO_OFF");
    }
}

}  // namespace

bool audioBT_init() {
    if (s_audioInitialized) {
        LOG_I(TAG, "Already initialized sink=%s", kSinkName);
        return true;
    }

    s_connected.store(false);
    reduceBtLogNoise();

#if A2DP_I2S_AUDIOTOOLS
    if (!configureAudioToolsOutput()) {
        LOG_E(TAG, "Failed to initialize AudioTools I2S output");
        shutdownAudio(false);
        return false;
    }

    s_a2dp.set_output(s_audioStream);
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

    s_a2dp.set_i2s_config(i2s_config);
    s_a2dp.set_pin_config(pin_config);
#else
    LOG_E(TAG, "No supported audio backend available");
    return false;
#endif

    configureSink();
    s_a2dp.start(kSinkName);

    if (!isBtStackReady()) {
        LOG_E(TAG,
              "Start failed controller=%d bluedroid=%d",
              (int)esp_bt_controller_get_status(),
              (int)esp_bluedroid_get_status());
        shutdownAudio(false);
        return false;
    }

    s_audioInitialized = true;
    RAM_CHECKPOINT("AUDIO_ON");
    return true;
}

void audioBT_deinit() {
    shutdownAudio(true);
}

bool audioBT_isConnected() {
    return s_connected.load();
}

TaskHandle_t audioBT_getI2STaskHandle() {
    return s_audioInitialized ? s_a2dp.getI2STaskHandle() : nullptr;
}
