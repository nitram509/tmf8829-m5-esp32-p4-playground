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

#ifdef __cplusplus
}
#endif

#endif // NET_H
