//
// Created by Martin W. Kirst on 17.10.26.
//

#ifndef IMAGE_H
#define IMAGE_H

#include <cstdint>

/** @brief Initializes the shared distance map storage and synchronization mutex. */
void init_distance_map_storage();

/** @brief Polls and processes in-flight measurement frames from the sensor FIFO during continuous measurement.
 *  @return APP_SUCCESS_OK if frames were processed successfully, or error code on failure.
 */
int8_t process_sensor_measurement_stream();

/** @brief Retrieves a copy of the latest available distance map from RAM in a thread-safe manner.
 *  @param out_distances Pointer to destination buffer (must hold at least 1536 uint16 values).
 *  @param out_width Pointer to receive width of the latest frame (e.g. 8, 16, 32, 48).
 *  @param out_height Pointer to receive height of the latest frame (e.g. 8, 16, 32).
 *  @param out_timestamp_us Optional output pointer to receive timestamp (in microseconds) of the latest frame.
 *  @param out_frame_counter Optional output pointer to receive sequential frame counter.
 *  @return true if a valid frame was copied, false if no frame is available yet or mutex timeout.
 */
bool get_latest_distance_map(uint16_t *out_distances, uint16_t *out_width, uint16_t *out_height, int64_t *out_timestamp_us = nullptr, uint32_t *out_frame_counter = nullptr);

/** @brief Retrieves a copy of the latest available SNR (signal-to-noise ratio) map from RAM in a thread-safe manner.
 *  @param out_snr Pointer to destination buffer (must hold at least 1536 uint8 values).
 *  @param out_width Pointer to receive width of the latest frame (e.g. 8, 16, 32, 48).
 *  @param out_height Pointer to receive height of the latest frame (e.g. 8, 16, 32).
 *  @param out_timestamp_us Optional output pointer to receive timestamp (in microseconds) of the latest frame.
 *  @param out_frame_counter Optional output pointer to receive sequential frame counter.
 *  @return true if a valid frame was copied, false if no frame is available yet or mutex timeout.
 */
bool get_latest_snr_map(uint8_t *out_snr, uint16_t *out_width, uint16_t *out_height, int64_t *out_timestamp_us = nullptr, uint32_t *out_frame_counter = nullptr);

/** @brief Helper to get expected dimensions for a given configuration command code. */
void get_sensor_mode_dimensions(uint8_t cfgCmd, uint16_t *out_width, uint16_t *out_height);

/** @brief Checks if at least one complete valid distance map has been captured and stored.
 *  @return true if depth data is available, false otherwise.
 */
bool is_distance_map_available();

/** @brief Retrieves the latest reported temperatures (in °C) from the three on-chip sensors.
 *  @param out_temp0 Pointer to output temperature sensor 0 (°C).
 *  @param out_temp1 Pointer to output temperature sensor 1 (°C).
 *  @param out_temp2 Pointer to output temperature sensor 2 (°C).
 *  @return true if valid temperature readings are available, false otherwise.
 */
bool get_latest_temperatures(int8_t *out_temp0, int8_t *out_temp1, int8_t *out_temp2);

/** @brief Retrieves real-time sensor telemetry including temperatures, FPS, dimensions, and frame counter.
 *  @param out_temp0 Output temperature sensor 0 (°C).
 *  @param out_temp1 Output temperature sensor 1 (°C).
 *  @param out_temp2 Output temperature sensor 2 (°C).
 *  @param out_fps Output real-time FPS calculation.
 *  @param out_width Output active frame width.
 *  @param out_height Output active frame height.
 *  @param out_frame_counter Output total frame count.
 *  @return true if telemetry is available, false otherwise.
 */
bool get_sensor_telemetry(int8_t *out_temp0, int8_t *out_temp1, int8_t *out_temp2,
                          float *out_fps, uint16_t *out_width, uint16_t *out_height,
                          uint32_t *out_frame_counter);

struct CaptureTimings {
    int64_t activate_us;
    int64_t grab_us;
    int64_t disable_us;
};

int8_t captureDistanceMap48x32(uint16_t *distances, uint16_t width, uint16_t height, CaptureTimings *timings = nullptr);

#endif //IMAGE_H
