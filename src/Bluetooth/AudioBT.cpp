#include "AudioBT.h"
#include "BluetoothA2DPSinkQueued.h"
#include "esp_bt.h"
#include "esp_bt_main.h"
#include "esp_log.h"
#include "BoardPins.h"
#include "TaskConfig.h"
#include "AppLog.h"
#include "RamTelemetry.h"
#include "../comms/esp_to_gution/Esptogution.h"
#include "../comms/esp_to_gution/EsptoGuitionState.h"
#include "EQFilter.h"

#include <atomic>


#if A2DP_I2S_AUDIOTOOLS
static audio_tools::I2SStream s_audioStream;
static bool s_audioStreamActive = false;
#endif

#if defined(ARDUINO_ARCH_ESP32)
extern "C" bool btInUse() {
    return true;
}
#endif

namespace {

constexpr char TAG[] = "BT";
constexpr char kSinkName[] = "ESP32-BASS";

constexpr int kSampleRateHz = 44100;
constexpr int kBitsPerSample = 16;
constexpr int kChannelCount = 2;
constexpr int kI2sBufferCount = 8;
constexpr int kI2sBufferSize = 256;
constexpr int kRingbufferSizeBytes = 16 * 1024;
constexpr int kRingbufferPrefetchPercent = 80;
constexpr size_t kI2sWriteSizeUpto = 240 * 8;
constexpr int kI2sTicks = 10;

static BluetoothA2DPSinkQueued s_a2dp;
static std::atomic<bool> s_connected{false};
static std::atomic<bool> s_isPlaying{false};
static bool s_audioInitialized = false;
static uint32_t s_musicPlayingTimeMs = 0;
static char s_musicTitle[128] = {0};
static char s_musicArtist[128] = {0};
static char s_musicAlbum[128] = {0};
static char s_musicTrack[32] = {0};
static char s_musicTracks[32] = {0};
static char s_musicGenre[64] = {0};
static portMUX_TYPE s_metadataLock = portMUX_INITIALIZER_UNLOCKED;

// Deferred UART sends from BT callbacks (AVRCP runs on a BT task, not the main loop).
// Callbacks set bits; audioBT_serviceDeferred() flushes them from the main loop.
static std::atomic<uint8_t> s_deferredSend{0};
enum DeferredBtSend : uint8_t {
    kSendTitle     = 1 << 0,
    kSendArtist    = 1 << 1,
    kSendStatus    = 1 << 2,
    kSendLogLine   = 1 << 3,
};

static void deferSend(DeferredBtSend bit) {
    s_deferredSend.store(s_deferredSend.load(std::memory_order_relaxed) | static_cast<uint8_t>(bit),
                         std::memory_order_release);
}

void resetMusicMetadata() {
    s_musicPlayingTimeMs = 0;
    s_musicTitle[0] = '\0';
    s_musicArtist[0] = '\0';
    s_musicAlbum[0] = '\0';
    s_musicTrack[0] = '\0';
    s_musicTracks[0] = '\0';
    s_musicGenre[0] = '\0';
}

const char *metadataAttrName(uint8_t attrId) {
    switch (attrId) {
        case ESP_AVRC_MD_ATTR_TITLE:
            return "title";
        case ESP_AVRC_MD_ATTR_ARTIST:
            return "artist";
        case ESP_AVRC_MD_ATTR_ALBUM:
            return "album";
        case ESP_AVRC_MD_ATTR_TRACK_NUM:
            return "track";
        case ESP_AVRC_MD_ATTR_NUM_TRACKS:
            return "tracks";
        case ESP_AVRC_MD_ATTR_GENRE:
            return "genre";
        case ESP_AVRC_MD_ATTR_PLAYING_TIME:
            return "playing_time";
        default:
            return "attr";
    }
}

void copyMetadataField(char *destination, size_t destinationSize, const uint8_t *text) {
    if (destination == nullptr || destinationSize == 0) {
        return;
    }

    destination[0] = '\0';
    if (text == nullptr) {
        return;
    }

    const char *source = reinterpret_cast<const char *>(text);
    std::snprintf(destination, destinationSize, "%s", source);
}

void printMusicMetadataLine() {
    char durationText[16] = {0};
    if (s_musicPlayingTimeMs > 0) {
        const uint32_t totalSeconds = s_musicPlayingTimeMs / 1000U;
        const uint32_t minutes = totalSeconds / 60U;
        const uint32_t seconds = totalSeconds % 60U;
        std::snprintf(durationText, sizeof(durationText), "%02lu:%02lu",
                      static_cast<unsigned long>(minutes),
                      static_cast<unsigned long>(seconds));
    } else {
        std::snprintf(durationText, sizeof(durationText), "n/a");
    }

    Serial.printf("[BT][I] Track title=%s artist=%s album=%s track=%s tracks=%s genre=%s duration=%s\r\n",
                  (s_musicTitle[0] != '\0') ? s_musicTitle : "n/a",
                  (s_musicArtist[0] != '\0') ? s_musicArtist : "n/a",
                  (s_musicAlbum[0] != '\0') ? s_musicAlbum : "n/a",
                  (s_musicTrack[0] != '\0') ? s_musicTrack : "n/a",
                  (s_musicTracks[0] != '\0') ? s_musicTracks : "n/a",
                  (s_musicGenre[0] != '\0') ? s_musicGenre : "n/a",
                  durationText);
}

void metadata_callback(uint8_t attrId, const uint8_t *text) {
    if (text == nullptr) {
        return;
    }

    switch (attrId) {
        case ESP_AVRC_MD_ATTR_TITLE:
            portENTER_CRITICAL(&s_metadataLock);
            copyMetadataField(s_musicTitle, sizeof(s_musicTitle), text);
            portEXIT_CRITICAL(&s_metadataLock);
            deferSend(kSendTitle);
            break;
        case ESP_AVRC_MD_ATTR_ARTIST:
            portENTER_CRITICAL(&s_metadataLock);
            copyMetadataField(s_musicArtist, sizeof(s_musicArtist), text);
            portEXIT_CRITICAL(&s_metadataLock);
            deferSend(kSendArtist);
            break;
        case ESP_AVRC_MD_ATTR_ALBUM:
            copyMetadataField(s_musicAlbum, sizeof(s_musicAlbum), text);
            break;
        case ESP_AVRC_MD_ATTR_TRACK_NUM:
            copyMetadataField(s_musicTrack, sizeof(s_musicTrack), text);
            break;
        case ESP_AVRC_MD_ATTR_NUM_TRACKS:
            copyMetadataField(s_musicTracks, sizeof(s_musicTracks), text);
            break;
        case ESP_AVRC_MD_ATTR_GENRE:
            copyMetadataField(s_musicGenre, sizeof(s_musicGenre), text);
            break;
        case ESP_AVRC_MD_ATTR_PLAYING_TIME:
            s_musicPlayingTimeMs = static_cast<uint32_t>(std::strtoul(reinterpret_cast<const char *>(text), nullptr, 10));
            break;
        default:
            break;
    }

    deferSend(kSendLogLine);
}

void connection_state_callback(esp_a2d_connection_state_t state, void*) {
    const bool wasConnected = s_connected.load();
    const bool nowConnected = (state == ESP_A2D_CONNECTION_STATE_CONNECTED);
    s_connected.store(nowConnected);

    if (state == ESP_A2D_CONNECTION_STATE_DISCONNECTED) {
        s_isPlaying.store(false);
    }

    if (nowConnected != wasConnected) {
        if (!nowConnected) {
            portENTER_CRITICAL(&s_metadataLock);
            resetMusicMetadata();
            portEXIT_CRITICAL(&s_metadataLock);
            deferSend(kSendTitle);
            deferSend(kSendArtist);
        }
        deferSend(kSendStatus);
    }
}

void play_status_callback(esp_avrc_playback_stat_t playback) {
    bool playing = (playback == ESP_AVRC_PLAYBACK_PLAYING);
    s_isPlaying.store(playing);
    deferSend(kSendStatus);
}

void track_change_callback(uint8_t *) {
    portENTER_CRITICAL(&s_metadataLock);
    resetMusicMetadata();
    portEXIT_CRITICAL(&s_metadataLock);
    deferSend(kSendTitle);
    deferSend(kSendArtist);
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
    s_a2dp.set_i2s_stack_size(TaskConfig::BtI2STask::kStackBytes);
    s_a2dp.set_i2s_write_size_upto(kI2sWriteSizeUpto);
    s_a2dp.set_i2s_ticks(kI2sTicks);
    s_a2dp.set_avrc_metadata_callback(metadata_callback);
    s_a2dp.set_avrc_rn_playstatus_callback(play_status_callback);
    s_a2dp.set_avrc_rn_track_change_callback(track_change_callback);

    // Keep BT control and audio on Core 0; encoder, I2C, and WiFi stay on Core 1.
    s_a2dp.set_task_core(TaskConfig::BtAppTask::kCore);
    s_a2dp.set_task_priority(TaskConfig::BtAppTask::kPriority);
    s_a2dp.set_event_queue_size(TaskConfig::BtAppTask::kEventQueueSize);
    s_a2dp.set_event_stack_size(TaskConfig::BtAppTask::kStackBytes);
    s_a2dp.set_i2s_task_priority(TaskConfig::BtI2STask::kPriority);
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
    resetMusicMetadata();
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
    
    // Apply initial volume from NVS settings
    audioBT_setVolume(EsptoGuition::getMusicVolume());

    RAM_CHECKPOINT("AUDIO_ON");
    return true;
}

void audioBT_deinit() {
    shutdownAudio(true);
}

bool audioBT_isConnected() {
    return s_connected.load();
}

TaskHandle_t audioBT_getAppTaskHandle() {
    return s_audioInitialized ? s_a2dp.getAppTaskHandle() : nullptr;
}

TaskHandle_t audioBT_getI2STaskHandle() {
    return s_audioInitialized ? s_a2dp.getI2STaskHandle() : nullptr;
}

bool audioBT_play() {
    if (!s_audioInitialized || !s_connected.load()) {
        return false;
    }

    s_a2dp.play();
    s_isPlaying.store(true);
    return true;
}

bool audioBT_pause() {
    if (!s_audioInitialized || !s_connected.load()) {
        return false;
    }

    s_a2dp.pause();
    s_isPlaying.store(false);
    return true;
}

bool audioBT_previous() {
    if (!s_audioInitialized || !s_connected.load()) {
        return false;
    }

    s_a2dp.previous();
    return true;
}

bool audioBT_next() {
    if (!s_audioInitialized || !s_connected.load()) {
        return false;
    }

    s_a2dp.next();
    return true;
}

bool audioBT_volumeDown() {
    if (!s_audioInitialized) {
        return false;
    }

    s_a2dp.volume_down();
    return true;
}

bool audioBT_volumeUp() {
    if (!s_audioInitialized) {
        return false;
    }

    s_a2dp.volume_up();
    return true;
}

const char* audioBT_getTitle() {
    return s_musicTitle;
}

const char* audioBT_getArtist() {
    return s_musicArtist;
}

void audioBT_copyMetadata(char* title, size_t titleSize, char* artist, size_t artistSize) {
    portENTER_CRITICAL(&s_metadataLock);
    snprintf(title, titleSize, "%s", s_musicTitle);
    snprintf(artist, artistSize, "%s", s_musicArtist);
    portEXIT_CRITICAL(&s_metadataLock);
}

bool audioBT_isPlaying() {
    return s_isPlaying.load();
}

void audioBT_setVolume(uint8_t vol) {
    if (!s_audioInitialized) return;
    if (vol > 100) vol = 100;
    s_a2dp.set_volume(vol * 127 / 100);
    EsptoGuition::sendMusicVolumeState(vol);
}

void audioBT_serviceDeferred() {
    const uint8_t pending = s_deferredSend.exchange(0, std::memory_order_acq_rel);
    if (pending == 0) return;

    if (pending & kSendTitle) {
        char title[128];
        portENTER_CRITICAL(&s_metadataLock);
        std::snprintf(title, sizeof(title), "%s", s_musicTitle);
        portEXIT_CRITICAL(&s_metadataLock);
        EsptoGuition::sendMusicTitle(title);
    }
    if (pending & kSendArtist) {
        char artist[128];
        portENTER_CRITICAL(&s_metadataLock);
        std::snprintf(artist, sizeof(artist), "%s", s_musicArtist);
        portEXIT_CRITICAL(&s_metadataLock);
        EsptoGuition::sendMusicArtist(artist);
    }
    if (pending & kSendStatus) {
        const bool connected = s_connected.load(std::memory_order_acquire);
        const bool playing   = s_isPlaying.load(std::memory_order_acquire);
        EsptoGuition::sendMusicStatus(connected, playing);
    }
    if (pending & kSendLogLine) {
        printMusicMetadataLine();
    }
}

void audioBT_setEQ(uint8_t bass, uint8_t mid, uint8_t treble) {
    if (bass > 100) bass = 100;
    if (mid > 100) mid = 100;
    if (treble > 100) treble = 100;

    updateEQFilters(bass, mid, treble);

    EsptoGuition::sendMusicEQState(bass, mid, treble);
}

