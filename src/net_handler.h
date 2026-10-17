#ifndef NET_HANDLER_H
#define NET_HANDLER_H

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize and start the UDP network handler task listening on the configured port.
 * @return ESP_OK on success, or error code on failure.
 */
esp_err_t start_net_handler(void);

/**
 * @brief Stop the UDP network handler task and close the socket.
 */
void stop_net_handler(void);

#ifdef __cplusplus
}
#endif

#endif // NET_HANDLER_H
