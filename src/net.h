#ifndef NET_H
#define NET_H

#include <cstdint>
#include <cstddef>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize Ethernet interface, TCP/IP stack, and DHCP client.
 * @return ESP_OK on success, or error code on failure.
 */
esp_err_t init_network(void);

/**
 * @brief Check if network interface has obtained an IP address.
 * @return true if connected with valid IP, false otherwise.
 */
bool is_network_connected(void);

/**
 * @brief Send raw frame payload data via UDP to configured target host and port.
 * @param frameData Pointer to raw frame payload data.
 * @param length Length of data in bytes.
 * @return ESP_OK on success, or error code on failure.
 */
esp_err_t send_raw_frame_udp(const uint8_t *frameData, size_t length);

/**
 * @brief Send ASCII PGM (P2) distance map image via UDP to configured target host and port.
 * @param distances Pointer to array of uint16 distance values in row-major order.
 * @param width Number of columns (width in pixels).
 * @param height Number of rows (height in pixels).
 * @return ESP_OK on success, or error code on failure.
 */
esp_err_t send_distance_map_pgm_udp(const uint16_t *distances, uint16_t width, uint16_t height);

/**
 * @brief Start the periodic UDP beacon sending device info and IP address (first 3 at 5s interval, then every 60s).
 * @return ESP_OK on success, or error code on failure.
 */
esp_err_t start_device_info_beacon(void);

/**
 * @brief Stop the periodic UDP device info beacon task.
 */
void stop_device_info_beacon(void);

#ifdef __cplusplus
}
#endif

#endif // NET_H
