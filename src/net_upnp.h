#ifndef NET_UPNP_H
#define NET_UPNP_H

#include "esp_err.h"
#include <cstddef>
#include <cstdint>

#ifdef __cplusplus
extern "C" {
#endif

#define UPNP_DEVICE_NAME "esp32-p4-tmf8829"
#define UPNP_DEVICE_TYPE "urn:de-bitkings:device:esp32-p4-tmf8829:1"

/**
 * @brief Get the formatted UUID string derived from the 6-byte MAC address repeated to fill 16 bytes.
 * @param buf Output buffer (at least 37 bytes).
 * @param max_len Size of output buffer.
 * @param mac 6-byte MAC address.
 */
void get_device_uuid_str(char *buf, size_t max_len, const uint8_t *mac);

/**
 * @brief Start periodic UPnP SSDP NOTIFY broadcast task (sends alive notification every 5 seconds).
 * @return ESP_OK on success, or error code on failure.
 */
esp_err_t start_upnp_broadcast(void);

/**
 * @brief Stop the periodic UPnP SSDP NOTIFY broadcast task and send ssdp:byebye announcement.
 */
void stop_upnp_broadcast(void);

/**
 * @brief Check if UPnP broadcast task is currently active.
 * @return true if running, false otherwise.
 */
bool is_upnp_broadcast_running(void);

/**
 * @brief Generate the XML description for UPNP device
 * @param buffer Output buffer (at least 600 bytes).
 * @param max_len Size of output buffer.
 * @return length of generated XML description, or negative error code on failure.
 */
int generate_upnp_device_description_xml(char *buffer, size_t max_len) ;

#ifdef __cplusplus
}
#endif

#endif // NET_UPNP_H
