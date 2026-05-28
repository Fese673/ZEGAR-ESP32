
#include "BluetoothA2DPSinkQueued.h"

#include "RuntimeTelemetry.h"
#include "EQFilter.h"

#if IS_VALID_PLATFORM

void BluetoothA2DPSinkQueued::bt_i2s_task_start_up(void) {
    ESP_LOGI(BT_APP_TAG, "ringbuffer data empty! mode changed: RINGBUFFER_MODE_PREFETCHING");
    ringbuffer_mode.store(RINGBUFFER_MODE_PREFETCHING);
    bt_audio_active.store(false);
    needs_ringbuffer_reset.store(false);
    s_pendingI2sRestart.store(false, std::memory_order_relaxed);
    if ((s_i2s_write_semaphore = xSemaphoreCreateBinary()) == nullptr) {
        ESP_LOGE(BT_APP_TAG, "%s, Semaphore create failed", __func__);
        return;
    }
    if ((s_ringbuf_i2s = xRingbufferCreate(i2s_ringbuffer_size, RINGBUF_TYPE_BYTEBUF)) == nullptr) {
        ESP_LOGE(BT_APP_TAG, "%s, ringbuffer create failed", __func__);
        vSemaphoreDelete(s_i2s_write_semaphore);
        s_i2s_write_semaphore = nullptr;
        return;
    }
    BaseType_t result = xTaskCreatePinnedToCore(ccall_i2s_task_handler, "BtI2STask", i2s_stack_size, nullptr, i2s_task_priority, &s_bt_i2s_task_handle, task_core);
    if (result!=pdPASS){
        ESP_LOGE(BT_AV_TAG, "xTaskCreatePinnedToCore");
        vRingbufferDelete(s_ringbuf_i2s);
        s_ringbuf_i2s = nullptr;
        vSemaphoreDelete(s_i2s_write_semaphore);
        s_i2s_write_semaphore = nullptr;
    } else {
        ESP_LOGI(BT_AV_TAG, "BtI2STask Started");
    }
}

void BluetoothA2DPSinkQueued::bt_i2s_task_shut_down(void) {
    bt_audio_active.store(false);
    needs_ringbuffer_reset.store(false);
    s_pendingI2sRestart.store(false, std::memory_order_relaxed);
    if (s_bt_i2s_task_handle) {
        vTaskDelete(s_bt_i2s_task_handle);
        s_bt_i2s_task_handle = nullptr;
    }
    if (s_ringbuf_i2s) {
        vRingbufferDelete(s_ringbuf_i2s);
        s_ringbuf_i2s = nullptr;
    }
    if (s_i2s_write_semaphore) {
        vSemaphoreDelete(s_i2s_write_semaphore);
        s_i2s_write_semaphore = nullptr;
    }

    ESP_LOGI(BT_AV_TAG, "BtI2STask shutdown");
}

/* NEW I2S Task & ring buffer */

void BluetoothA2DPSinkQueued::drain_ringbuffer(void) {
    if (s_ringbuf_i2s == nullptr) {
        return;
    }

    size_t item_size = 0;
    uint8_t *data = nullptr;

    do {
        item_size = 0;
        data = (uint8_t *)xRingbufferReceiveUpTo(s_ringbuf_i2s, &item_size, 0,
                                                 i2s_write_size_upto);
        if (item_size > 0 && data != nullptr) {
            vRingbufferReturnItem(s_ringbuf_i2s, (void *)data);
        }
    } while (item_size > 0);
}

void BluetoothA2DPSinkQueued::i2s_task_handler(void *arg) {
    uint8_t *data = nullptr;
    size_t item_size = 0;
    /**
     * The total length of DMA buffer of I2S is:
     * `dma_frame_num * dma_desc_num * i2s_channel_num * i2s_data_bit_width / 8`.
     * Transmit `dma_frame_num * dma_desc_num` bytes to DMA is trade-off.
     */
    is_starting.store(true);

    while (true) {
        if (needs_ringbuffer_reset.load() && s_ringbuf_i2s != nullptr) {
            drain_ringbuffer();
            needs_ringbuffer_reset.store(false);
        }

        if (!bt_audio_active.load()) {
            ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
            continue;
        }

        // Deferred I2S restart requested by write_audio() — run from I2S task context
        if (s_pendingI2sRestart.exchange(false, std::memory_order_acq_rel)) {
            if (out->begin()) {
                is_i2s_active.store(true, std::memory_order_release);
                ESP_LOGI(BT_AV_TAG, "i2s restarted via deferred request");
            } else {
                ESP_LOGE(BT_APP_TAG, "i2s deferred restart failed");
            }
        }

        if (is_starting.load()){
            // wait for ringbuffer to be filled
            if (pdTRUE != xSemaphoreTake(s_i2s_write_semaphore,
                                         pdMS_TO_TICKS(A2DP_I2S_STARTUP_WAIT_MS))){
                if (!bt_audio_active.load()) {
                    continue;
                }
                TELEMETRY_INC(audio_underruns);
                ESP_LOGW(BT_APP_TAG, "prefetch wait timed out, retrying");
                continue;
            }
            if (!bt_audio_active.load()) {
                continue;
            }
            s_bassFilter.reset();
            s_midFilter.reset();
            s_trebleFilter.reset();
            is_starting.store(false);
        }
        // xSemaphoreTake was succeeding here, so we have the buffer filled up
        item_size = 0;

        // receive data from ringbuffer and write it to I2S DMA transmit buffer 
        data = (uint8_t *)xRingbufferReceiveUpTo(s_ringbuf_i2s, &item_size, (TickType_t)pdMS_TO_TICKS(i2s_ticks), i2s_write_size_upto);
        if (item_size == 0) {
            if (!bt_audio_active.load()) {
                continue;
            }
            TELEMETRY_INC(audio_underruns);
            if (ringbuffer_mode.load() != RINGBUFFER_MODE_PREFETCHING) {
                // Underflow - silent mode switch
                ringbuffer_mode.store(RINGBUFFER_MODE_PREFETCHING);
                ESP_LOGW(BT_APP_TAG, "ringbuffer underrun, mode changed: RINGBUFFER_MODE_PREFETCHING");
            }
            continue;
        } 

        // if i2s is not active we just consume the buffer w/o output
        if (is_i2s_active.load() && is_output){
            int16_t *samples = reinterpret_cast<int16_t*>(data);
            int n = (item_size / 2) & ~1; // stereo frame aligned

            int bassIdx = s_bassFilter.activeIdx.load(std::memory_order_acquire);
            int midIdx = s_midFilter.activeIdx.load(std::memory_order_acquire);
            int trebleIdx = s_trebleFilter.activeIdx.load(std::memory_order_acquire);

            const BiquadCoeffs &cBass = s_bassFilter.coeffs[bassIdx];
            const BiquadCoeffs &cMid = s_midFilter.coeffs[midIdx];
            const BiquadCoeffs &cTreble = s_trebleFilter.coeffs[trebleIdx];

            bool applyBass = !cBass.bypass;
            bool applyMid = !cMid.bypass;
            bool applyTreble = !cTreble.bypass;

            if (n >= 2 && (applyBass || applyMid || applyTreble)) {
                for (int i = 0; i < n; i += 2) {
                    float l = static_cast<float>(samples[i]);
                    float r = static_cast<float>(samples[i+1]);

                    if (applyBass) {
                        l = s_bassFilter.processLeft(l, cBass);
                        r = s_bassFilter.processRight(r, cBass);
                    }
                    if (applyMid) {
                        l = s_midFilter.processLeft(l, cMid);
                        r = s_midFilter.processRight(r, cMid);
                    }
                    if (applyTreble) {
                        l = s_trebleFilter.processLeft(l, cTreble);
                        r = s_trebleFilter.processRight(r, cTreble);
                    }

                    // Soft-Clipping Guard
                    if (l > 32767.0f) l = 32767.0f;
                    else if (l < -32768.0f) l = -32768.0f;

                    if (r > 32767.0f) r = 32767.0f;
                    else if (r < -32768.0f) r = -32768.0f;

                    samples[i] = static_cast<int16_t>(l);
                    samples[i+1] = static_cast<int16_t>(r);
                }
            }

            size_t written = i2s_write_data(data, item_size);
            if (written==0){
                ESP_LOGE(BT_APP_TAG, "i2s_write_data failed %d->%d", item_size, written);
            } else if (written < item_size) {
                ESP_LOGW(BT_APP_TAG, "i2s_write_data truncated %d->%d", item_size, written);
            }
        }

        vRingbufferReturnItem(s_ringbuf_i2s, (void *)data);
    }
}

size_t BluetoothA2DPSinkQueued::write_audio(const uint8_t *data, size_t size)
{
    size_t item_size = 0;
    BaseType_t done = pdFALSE;

    if (s_ringbuf_i2s == nullptr) {
        TELEMETRY_INC(audio_drops);
        ESP_LOGW(BT_APP_TAG, "ringbuffer not ready, drop audio packet");
        return 0;
    }

    if (!bt_audio_active.load()) {
        return 0;
    }

    // This should not really happen!
    // Defer the I2S restart to i2s_task_handler (safe context) instead of
    // calling out->begin() from the BT callback (priority 24).
    if (!is_i2s_active.load()){
        s_pendingI2sRestart.store(true, std::memory_order_release);
        TELEMETRY_INC(audio_drops);
        ESP_LOGW(BT_APP_TAG, "i2s inactive: deferred restart requested");
        return 0;
    }

    if (ringbuffer_mode.load() == RINGBUFFER_MODE_DROPPING) {
        TELEMETRY_INC(audio_drops);
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(4, 0, 0)
        vRingbufferGetInfo(s_ringbuf_i2s, nullptr, nullptr, nullptr, nullptr, &item_size);
#else
        vRingbufferGetInfo(s_ringbuf_i2s, nullptr, nullptr, nullptr, &item_size);
#endif
        if (item_size <= i2s_ringbuffer_prefetch_size()) {
            ESP_LOGI(BT_APP_TAG, "ringbuffer data decreased! mode changed: RINGBUFFER_MODE_PROCESSING");
            ringbuffer_mode.store(RINGBUFFER_MODE_PROCESSING);
        }
        return 0;
    }

    done = xRingbufferSend(s_ringbuf_i2s, (void *)data, size, (TickType_t)0);

    if (!done) {
        TELEMETRY_INC(audio_overflows);
        ESP_LOGW(BT_APP_TAG, "ringbuffer overflowed, ready to decrease data! mode changed: RINGBUFFER_MODE_DROPPING");
        ringbuffer_mode.store(RINGBUFFER_MODE_DROPPING);
        TELEMETRY_INC(audio_drops);
    }

    if (ringbuffer_mode.load() == RINGBUFFER_MODE_PREFETCHING) {
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(4, 0, 0)
        vRingbufferGetInfo(s_ringbuf_i2s, nullptr, nullptr, nullptr, nullptr, &item_size);
#else
        vRingbufferGetInfo(s_ringbuf_i2s, nullptr, nullptr, nullptr, &item_size);
#endif

        if (item_size >= i2s_ringbuffer_prefetch_size()) {
            ESP_LOGI(BT_APP_TAG, "ringbuffer data increased! mode changed: RINGBUFFER_MODE_PROCESSING");
            ringbuffer_mode.store(RINGBUFFER_MODE_PROCESSING);
            if (pdFALSE == xSemaphoreGive(s_i2s_write_semaphore)) {
                ESP_LOGE(BT_APP_TAG, "semphore give failed");
            }
        }
    }

    return done ? size : 0;
}

#endif // platform
