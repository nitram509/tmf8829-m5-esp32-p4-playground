//
// Created by Martin W. Kirst on 17.10.26.
//

#ifndef IMAGE_PGM_H
#define IMAGE_PGM_H

#include <cstdint>
#include <cstddef>

/** @brief Initializes the shared distance map storage and synchronization mutex. */
void init_distance_map_storage(void);

/** @brief Polls and processes in-flight measurement frames from the sensor FIFO during continuous measurement.
 *  @return APP_SUCCESS_OK if frames were processed successfully, or error code on failure.
 */
int8_t process_sensor_measurement_stream(void);

/** @brief Retrieves a copy of the latest available 48x32 distance map from RAM in a thread-safe manner.
 *  @param out_distances Pointer to destination buffer (must hold at least 1536 uint16 values).
 *  @param width Expected width (48).
 *  @param height Expected height (32).
 *  @param out_timestamp_us Optional output pointer to receive timestamp (in microseconds) of the latest frame.
 *  @param out_frame_counter Optional output pointer to receive sequential frame counter.
 *  @return true if a valid frame was copied, false if no frame is available yet or mutex timeout.
 */
bool get_latest_distance_map(uint16_t *out_distances, uint16_t width, uint16_t height, int64_t *out_timestamp_us = nullptr, uint32_t *out_frame_counter = nullptr);

/** @brief Checks if at least one complete valid distance map has been captured and stored.
 *  @return true if depth data is available, false otherwise.
 */
bool is_distance_map_available(void);

/** @brief Retrieves the latest reported temperatures (in °C) from the three on-chip sensors.
 *  @param out_temp0 Pointer to output temperature sensor 0 (°C).
 *  @param out_temp1 Pointer to output temperature sensor 1 (°C).
 *  @param out_temp2 Pointer to output temperature sensor 2 (°C).
 *  @return true if valid temperature readings are available, false otherwise.
 */
bool get_latest_temperatures(int8_t *out_temp0, int8_t *out_temp1, int8_t *out_temp2);

void printFramePGM(const uint8_t *frame, size_t frameLen);

void printDistanceMapPGM(const uint16_t *distances, uint16_t width, uint16_t height);

struct CaptureTimings {
    int64_t activate_us;
    int64_t grab_us;
    int64_t disable_us;
};

int8_t captureDistanceMap48x32(uint16_t *distances, uint16_t width, uint16_t height, CaptureTimings *timings = nullptr);

int8_t singleShot48x32HexDump();

int8_t singleShot8x8HexDump();

void printHexDump(const uint8_t *data, size_t len);

#endif //IMAGE_PGM_H
