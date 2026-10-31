#include <cstdio>
#include <cstring>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_timer.h"
#include "esp_log.h"

#include "tmf8829/tmf8829.h"
#include "tmf8829/tmf8829_shim.h"

#include "image.h"
#include "app.h"

static const char *TAG = "tmf8829";

static SemaphoreHandle_t s_distance_map_mutex = nullptr;
static uint16_t s_latest_distances[1536] = {0};
static uint8_t s_latest_snr[1536] = {0};
static bool s_subframes_seen[2] = {false, false};
static bool s_has_valid_frame = false;
static int64_t s_latest_frame_time_us = 0;
static uint32_t s_frame_counter = 0;
static int8_t s_latest_temperatures[3] = {0, 0, 0};
static bool s_has_temperatures = false;
static int64_t s_last_temperature_log_time_us = 0;

void init_distance_map_storage() {
    if (s_distance_map_mutex == nullptr) {
        s_distance_map_mutex = xSemaphoreCreateMutex();
    }
    memset(s_latest_distances, 0, sizeof(s_latest_distances));
    memset(s_latest_snr, 0, sizeof(s_latest_snr));
    s_subframes_seen[0] = false;
    s_subframes_seen[1] = false;
    s_has_valid_frame = false;
    s_latest_frame_time_us = 0;
    s_frame_counter = 0;
    s_latest_temperatures[0] = 0;
    s_latest_temperatures[1] = 0;
    s_latest_temperatures[2] = 0;
    s_has_temperatures = false;
    s_last_temperature_log_time_us = 0;
}

bool is_distance_map_available() {
    return s_has_valid_frame;
}

bool get_latest_temperatures(int8_t *out_temp0, int8_t *out_temp1, int8_t *out_temp2) {
    if (s_distance_map_mutex == nullptr || !s_has_temperatures) {
        return false;
    }
    if (xSemaphoreTake(s_distance_map_mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        if (out_temp0 != nullptr) *out_temp0 = s_latest_temperatures[0];
        if (out_temp1 != nullptr) *out_temp1 = s_latest_temperatures[1];
        if (out_temp2 != nullptr) *out_temp2 = s_latest_temperatures[2];
        xSemaphoreGive(s_distance_map_mutex);
        return true;
    }
    return false;
}

bool get_latest_distance_map(uint16_t *out_distances, uint16_t width, uint16_t height, int64_t *out_timestamp_us, uint32_t *out_frame_counter) {
    if (out_distances == nullptr || width != 48 || height != 32) {
        return false;
    }
    if (s_distance_map_mutex == nullptr || !s_has_valid_frame) {
        return false;
    }

    if (xSemaphoreTake(s_distance_map_mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        memcpy(out_distances, s_latest_distances, sizeof(uint16_t) * 1536);
        if (out_timestamp_us != nullptr) {
            *out_timestamp_us = s_latest_frame_time_us;
        }
        if (out_frame_counter != nullptr) {
            *out_frame_counter = s_frame_counter;
        }
        xSemaphoreGive(s_distance_map_mutex);
        return true;
    }
    return false;
}

bool get_latest_snr_map(uint8_t *out_snr, uint16_t width, uint16_t height, int64_t *out_timestamp_us, uint32_t *out_frame_counter) {
    if (out_snr == nullptr || width != 48 || height != 32) {
        return false;
    }
    if (s_distance_map_mutex == nullptr || !s_has_valid_frame) {
        return false;
    }

    if (xSemaphoreTake(s_distance_map_mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        memcpy(out_snr, s_latest_snr, sizeof(uint8_t) * 1536);
        if (out_timestamp_us != nullptr) {
            *out_timestamp_us = s_latest_frame_time_us;
        }
        if (out_frame_counter != nullptr) {
            *out_frame_counter = s_frame_counter;
        }
        xSemaphoreGive(s_distance_map_mutex);
        return true;
    }
    return false;
}

int8_t process_sensor_measurement_stream() {
    uint8_t intStatus = tmf8829GetAndClrInterrupts(&tmf8829, TMF8829_APP_INT_RESULTS | TMF8829_APP_INT_HISTOGRAMS);
    uint8_t fifoStatusReg = 0xFF;
    rxReg(&tmf8829, tmf8829.i2cSlaveAddress, TMF8829_COM_FIFOSTATUS, 1, &fifoStatusReg);
    bool fifoHasData = ((fifoStatusReg & 0x04) == 0); // Bit 2 (txfifo_empty): 0 = data in FIFO

    if (!(intStatus & (TMF8829_APP_INT_RESULTS | TMF8829_APP_INT_HISTOGRAMS)) && !fifoHasData) {
        return APP_SUCCESS_OK;
    }

    static uint8_t frameBuffer[4096];
    int8_t res = APP_SUCCESS_OK;

    while (true) {
        size_t totalFrameSize = 0;
        int8_t rxStat = rxReg(&tmf8829, tmf8829.i2cSlaveAddress, TMF8829_COM_FIFOSTATUS,
                              TMF8829_PRE_HEADER_SIZE + TMF8829_FRAME_HEADER_SIZE,
                              frameBuffer);
        if (rxStat != APP_SUCCESS_OK) {
            break;
        }

        uint8_t fid = frameBuffer[TMF8829_PRE_HEADER_SIZE] & TMF8829_FID_MASK;
        if (fid != TMF8829_FID_RESULTS && fid != TMF8829_FID_HISTOGRAMS) {
            break;
        }

        totalFrameSize = TMF8829_PRE_HEADER_SIZE + TMF8829_FRAME_HEADER_SIZE;
        uint8_t layout = frameBuffer[TMF8829_PRE_HEADER_SIZE + 1];
        uint16_t payload = tmf8829GetUint16(frameBuffer + TMF8829_PRE_HEADER_SIZE + 2);

        uint16_t sizeToRead = 0;
        if (payload >= (TMF8829_FRAME_HEADER_SIZE - TMF8829_FRAME_HEADER_OFFSET)) {
            sizeToRead = payload - (TMF8829_FRAME_HEADER_SIZE - TMF8829_FRAME_HEADER_OFFSET);
        }

        if (sizeToRead > 0 && (totalFrameSize + sizeToRead <= sizeof(frameBuffer))) {
            uint16_t remaining = sizeToRead;
            while (remaining > 0) {
                uint16_t chunk = (remaining > DATA_BUFFER_SIZE) ? DATA_BUFFER_SIZE : remaining;
                rxStat = rxReg(&tmf8829, tmf8829.i2cSlaveAddress, TMF8829_COM_FIFO, chunk,
                               frameBuffer + totalFrameSize);
                if (rxStat != APP_SUCCESS_OK) {
                    break;
                }
                totalFrameSize += chunk;
                remaining -= chunk;
            }
        }

        // Add clock correction pair
        uint32_t hTick = getSysTick();
        uint32_t tTick = tmf8829GetUint32(frameBuffer + 1);
        tmf8829ClockCorrectionAddPair(&tmf8829, hTick, tTick);

        // Extract temperatures reported in frame header bytes 8, 9, 10
        int8_t temp0 = static_cast<int8_t>(frameBuffer[TMF8829_PRE_HEADER_SIZE + 8]);
        int8_t temp1 = static_cast<int8_t>(frameBuffer[TMF8829_PRE_HEADER_SIZE + 9]);
        int8_t temp2 = static_cast<int8_t>(frameBuffer[TMF8829_PRE_HEADER_SIZE + 10]);

        if (s_distance_map_mutex != nullptr && xSemaphoreTake(s_distance_map_mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
            s_latest_temperatures[0] = temp0;
            s_latest_temperatures[1] = temp1;
            s_latest_temperatures[2] = temp2;
            s_has_temperatures = true;
            xSemaphoreGive(s_distance_map_mutex);
        } else {
            s_latest_temperatures[0] = temp0;
            s_latest_temperatures[1] = temp1;
            s_latest_temperatures[2] = temp2;
            s_has_temperatures = true;
        }

        int64_t now_us = esp_timer_get_time();
        if (now_us - s_last_temperature_log_time_us >= 5000000) {
            s_last_temperature_log_time_us = now_us;
            ESP_LOGI(TAG, "TMF8829 Temperatures: T0=%d °C, T1=%d °C, T2=%d °C", temp0, temp1, temp2);
        }

        if (fid == TMF8829_FID_RESULTS) {
            uint8_t subResult = 0;
            if (layout & TMF8829_RESULT_FRAME_SUBIDX_MASK) {
                subResult = 1;
            }

            uint8_t pixelSize = tmf8829GetPixelSize(layout);
            if (pixelSize == 0) {
                pixelSize = 3;
            }

            constexpr size_t pixelStart = TMF8829_PRE_HEADER_SIZE + TMF8829_FRAME_HEADER_SIZE;
            const size_t pixelDataBytes = (totalFrameSize > (pixelStart + TMF8829_FRAME_FOOTER_SIZE))
                                              ? (totalFrameSize - pixelStart - TMF8829_FRAME_FOOTER_SIZE)
                                              : 0;
            uint16_t totalPixels = pixelDataBytes / pixelSize;

            if (totalPixels == 768) {
                // 48x32 subframe (16 rows * 48 columns)
                if (s_distance_map_mutex != nullptr && xSemaphoreTake(s_distance_map_mutex, pdMS_TO_TICKS(20)) == pdTRUE) {
                    for (int m = 0; m < 768; m++) {
                        int r = m / 48; // row in subframe (0..15)
                        int c = m % 48; // col in subframe (0..47)
                        int y = (r * 2) + subResult; // full matrix row (0..31)
                        int fullIdx = y * 48 + c;

                        size_t offset = pixelStart + (m * pixelSize);
                        if ((offset + 2) <= totalFrameSize) {
                            size_t idx = 0;
                            if (layout & TMF8829_CFG_RESULT_FORMAT_NOISE_STRENGTH_MASK) { idx += 2; }
                            if (layout & TMF8829_CFG_RESULT_FORMAT_XTALK_MASK) { idx += 2; }
                            if ((offset + idx + 2) <= totalFrameSize) {
                                uint16_t dist = static_cast<uint16_t>(frameBuffer[offset + idx]) |
                                                (static_cast<uint16_t>(frameBuffer[offset + idx + 1]) << 8);
                                if (tmf8829.clkCorrectionEnable) {
                                    dist = tmf8829CorrectDistance(&tmf8829, dist);
                                }
                                s_latest_distances[fullIdx] = dist;
                            }
                            if ((offset + idx + 3) <= totalFrameSize) {
                                uint8_t snr = frameBuffer[offset + idx + 2];
                                s_latest_snr[fullIdx] = snr;
                            }
                        }
                    }
                    s_subframes_seen[subResult] = true;
                    if (s_subframes_seen[0] && s_subframes_seen[1]) {
                        s_has_valid_frame = true;
                    }
                    s_latest_frame_time_us = esp_timer_get_time();
                    s_frame_counter++;
                    xSemaphoreGive(s_distance_map_mutex);
                }
            } else if (totalPixels > 0 && totalPixels <= 1536) {
                // Single-frame mode (8x8, 16x16, 32x32, etc.)
                if (s_distance_map_mutex != nullptr && xSemaphoreTake(s_distance_map_mutex, pdMS_TO_TICKS(20)) == pdTRUE) {
                    for (uint16_t m = 0; m < totalPixels; m++) {
                        size_t offset = pixelStart + (m * pixelSize);
                        size_t idx = 0;
                        if (layout & TMF8829_CFG_RESULT_FORMAT_NOISE_STRENGTH_MASK) { idx += 2; }
                        if (layout & TMF8829_CFG_RESULT_FORMAT_XTALK_MASK) { idx += 2; }
                        if ((offset + idx + 2) <= totalFrameSize) {
                            uint16_t dist = static_cast<uint16_t>(frameBuffer[offset + idx]) |
                                            (static_cast<uint16_t>(frameBuffer[offset + idx + 1]) << 8);
                            if (tmf8829.clkCorrectionEnable) {
                                dist = tmf8829CorrectDistance(&tmf8829, dist);
                            }
                            s_latest_distances[m] = dist;
                        }
                        if ((offset + idx + 3) <= totalFrameSize) {
                            uint8_t snr = frameBuffer[offset + idx + 2];
                            s_latest_snr[m] = snr;
                        }
                    }
                    s_has_valid_frame = true;
                    s_latest_frame_time_us = esp_timer_get_time();
                    s_frame_counter++;
                    xSemaphoreGive(s_distance_map_mutex);
                }
            }

            // Print results if configured in driver log level
            if (tmf8829.logLevel >= TMF8829_LOG_LEVEL_RESULTS_HEADER) {
                handleReceivedFrameHeaderData(&tmf8829, frameBuffer);
                if (tmf8829.logLevel >= TMF8829_LOG_LEVEL_RESULTS) {
                    handleReceivedResultData(&tmf8829, frameBuffer + pixelStart, totalFrameSize - pixelStart);
                }
                handleReceivedResultDataEnd(&tmf8829);
            }
        } else if (fid == TMF8829_FID_HISTOGRAMS) {
            if (tmf8829.logLevel >= TMF8829_LOG_LEVEL_RESULTS_HEADER) {
                handleReceivedFrameHeaderData(&tmf8829, frameBuffer);
                if (tmf8829.logLevel >= TMF8829_LOG_LEVEL_RESULTS) {
                    handleReceivedHistogramData(&tmf8829, frameBuffer + (TMF8829_PRE_HEADER_SIZE + TMF8829_FRAME_HEADER_SIZE),
                                                totalFrameSize - (TMF8829_PRE_HEADER_SIZE + TMF8829_FRAME_HEADER_SIZE));
                }
                handleReceivedHistogramDataEnd(&tmf8829);
            }
        }

        // Check if more data in FIFO
        fifoStatusReg = 0xFF;
        rxReg(&tmf8829, tmf8829.i2cSlaveAddress, TMF8829_COM_FIFOSTATUS, 1, &fifoStatusReg);
        if ((fifoStatusReg & 0x04) != 0) {
            break;
        }
    }

    return res;
}

/** @brief Captures a single 48x32 distance map (even and odd sub-frames) from the sensor.
 *  @param distances Pointer to array of at least 1536 uint16 distance values.
 *  @param width Expected width (48).
 *  @param height Expected height (32).
 *  @return APP_SUCCESS_OK on success, or error code on failure.
 */
int8_t captureDistanceMap48x32(uint16_t *distances, uint16_t width, uint16_t height, CaptureTimings *timings) {
    if (distances == nullptr || width != 48 || height != 32) {
        return APP_ERROR_NO_RESULT;
    }

    if (stateTmf8829 == TMF8829_STATE_DISABLED || stateTmf8829 == TMF8829_STATE_ERROR) {
        return APP_ERROR_NO_RESULT;
    }

    if (stateTmf8829 == TMF8829_STATE_MEASURE) {
        tmf8829DisableInterrupts(&tmf8829, 0xFF);
        tmf8829StopMeasurement(&tmf8829);
        stateTmf8829 = TMF8829_STATE_STOPPED;
    }

    int64_t t_act_start = esp_timer_get_time();

    // 1. Set mode 48x32 Default
    currentMeasConfigNr = 7; // 48x32 Default mode (measConfig[7])
    preconfigure(TMF8829_CMD_STAT__cmd_stat__CMD_LOAD_CFG_48X32);

    // 2. Enable measurement
    tmf8829ClrAndEnableInterrupts(&tmf8829, TMF8829_APP_INT_RESULTS | TMF8829_APP_INT_HISTOGRAMS);
    int8_t startStat = tmf8829StartMeasurement(&tmf8829);
    int64_t t_act_end = esp_timer_get_time();

    if (startStat != APP_SUCCESS_OK) {
        tmf8829DisableInterrupts(&tmf8829, 0xFF);
        stateTmf8829 = TMF8829_STATE_STOPPED;
        return startStat;
    }
    stateTmf8829 = TMF8829_STATE_MEASURE;

    int64_t t_grab_start = esp_timer_get_time();

    // 3. Wait for 48x32 frames (2 sub-frames: even and odd rows)
    static uint8_t frameBuffer[4096] = {};
    bool subFrameSeen[2] = {false, false};
    int framesCaptured = 0;
    constexpr int timeoutMs = 15000;
    int elapsedMs = 0;

    while ((!subFrameSeen[0] || !subFrameSeen[1]) && elapsedMs < timeoutMs && framesCaptured < 4) {
        uint8_t intStatus = tmf8829GetAndClrInterrupts(&tmf8829, TMF8829_APP_INT_RESULTS | TMF8829_APP_INT_HISTOGRAMS);
        uint8_t fifoStatusReg = 0xFF;
        rxReg(&tmf8829, tmf8829.i2cSlaveAddress, TMF8829_COM_FIFOSTATUS, 1, &fifoStatusReg);
        bool fifoHasData = ((fifoStatusReg & 0x04) == 0); // Bit 2 (txfifo_empty): 0 = data available in FIFO

        if ((intStatus & TMF8829_APP_INT_RESULTS) || fifoHasData) {
            while ((!subFrameSeen[0] || !subFrameSeen[1]) && framesCaptured < 4) {
                size_t totalFrameSize = 0;
                int8_t rxStat = rxReg(&tmf8829, tmf8829.i2cSlaveAddress, TMF8829_COM_FIFOSTATUS,
                                      TMF8829_PRE_HEADER_SIZE + TMF8829_FRAME_HEADER_SIZE,
                                      frameBuffer);
                if (rxStat != APP_SUCCESS_OK) {
                    break;
                }

                uint8_t fid = frameBuffer[TMF8829_PRE_HEADER_SIZE] & TMF8829_FID_MASK;
                if (fid != TMF8829_FID_RESULTS && fid != TMF8829_FID_HISTOGRAMS) {
                    break;
                }

                totalFrameSize = TMF8829_PRE_HEADER_SIZE + TMF8829_FRAME_HEADER_SIZE;
                uint8_t layout = frameBuffer[TMF8829_PRE_HEADER_SIZE + 1];
                uint16_t payload = tmf8829GetUint16(frameBuffer + TMF8829_PRE_HEADER_SIZE + 2);

                uint16_t sizeToRead = 0;
                if (payload >= (TMF8829_FRAME_HEADER_SIZE - TMF8829_FRAME_HEADER_OFFSET)) {
                    sizeToRead = payload - (TMF8829_FRAME_HEADER_SIZE - TMF8829_FRAME_HEADER_OFFSET);
                }

                if (sizeToRead > 0 && (totalFrameSize + sizeToRead <= sizeof(frameBuffer))) {
                    uint16_t remaining = sizeToRead;
                    while (remaining > 0) {
                        uint16_t chunk = (remaining > DATA_BUFFER_SIZE) ? DATA_BUFFER_SIZE : remaining;
                        rxStat = rxReg(&tmf8829, tmf8829.i2cSlaveAddress, TMF8829_COM_FIFO, chunk,
                                       frameBuffer + totalFrameSize);
                        if (rxStat != APP_SUCCESS_OK) {
                            break;
                        }
                        totalFrameSize += chunk;
                        remaining -= chunk;
                    }
                }

                if (fid == TMF8829_FID_RESULTS) {
                    uint8_t subResult = 0;
                    if (layout & TMF8829_RESULT_FRAME_SUBIDX_MASK) {
                        subResult = 1;
                    } else if (subFrameSeen[0] && !subFrameSeen[1]) {
                        subResult = 1;
                    } else if (!subFrameSeen[0] && subFrameSeen[1]) {
                        subResult = 0;
                    }

                    uint8_t pixelSize = tmf8829GetPixelSize(layout);
                    if (pixelSize == 0) {
                        pixelSize = 3;
                    }

                    constexpr size_t pixelStart = TMF8829_PRE_HEADER_SIZE + TMF8829_FRAME_HEADER_SIZE;
                    constexpr int pixelsInSubFrame = 768; // 16 rows * 48 columns

                    for (int m = 0; m < pixelsInSubFrame; m++) {
                        int r = m / 48; // row in sub-frame (0..15)
                        int c = m % 48; // column (0..47)
                        int y = (r * 2) + subResult; // full matrix row (0..31)
                        int x = c;
                        int fullIdx = y * 48 + x;

                        size_t offset = pixelStart + (m * pixelSize);
                        if ((offset + 2) <= totalFrameSize) {
                            size_t idx = 0;
                            if (layout & TMF8829_CFG_RESULT_FORMAT_NOISE_STRENGTH_MASK) { idx += 2; }
                            if (layout & TMF8829_CFG_RESULT_FORMAT_XTALK_MASK) { idx += 2; }
                            if ((offset + idx + 2) <= totalFrameSize) {
                                uint16_t dist = static_cast<uint16_t>(frameBuffer[offset + idx]) | (
                                                    static_cast<uint16_t>(frameBuffer[offset + idx + 1]) << 8);
                                if (tmf8829.clkCorrectionEnable) {
                                    dist = tmf8829CorrectDistance(&tmf8829, dist);
                                }
                                distances[fullIdx] = dist;
                            }
                        }
                    }

                    subFrameSeen[subResult] = true;
                    framesCaptured++;
                }

                // Check if another frame is ready in the FIFO
                fifoStatusReg = 0xFF;
                rxReg(&tmf8829, tmf8829.i2cSlaveAddress, TMF8829_COM_FIFOSTATUS, 1, &fifoStatusReg);
                if ((fifoStatusReg & 0x04) != 0) {
                    break;
                }
            }
        }
        vTaskDelay(pdMS_TO_TICKS(5));
        elapsedMs += 5;
    }

    // 4. Stop measurement
    int64_t t_grab_end = esp_timer_get_time();
    int64_t t_dis_start = esp_timer_get_time();
    tmf8829DisableInterrupts(&tmf8829, 0xFF);
    tmf8829StopMeasurement(&tmf8829);
    stateTmf8829 = TMF8829_STATE_STOPPED;
    int64_t t_dis_end = esp_timer_get_time();

    if (timings != nullptr) {
        timings->activate_us = t_act_end - t_act_start;
        timings->grab_us = t_grab_end - t_grab_start;
        timings->disable_us = t_dis_end - t_dis_start;
    }

    if (!subFrameSeen[0] || !subFrameSeen[1]) {
        return APP_ERROR_NO_RESULT;
    }

    return APP_SUCCESS_OK;
}
