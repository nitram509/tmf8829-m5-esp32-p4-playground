#ifndef WEB_SERVER_H
#define WEB_SERVER_H

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize and start the HTTP server serving static / and /index.html.
 * @return ESP_OK on success, or error code on failure.
 */
esp_err_t start_webserver(void);

/**
 * @brief Stop the HTTP server and release resources.
 */
void stop_webserver(void);

#ifdef __cplusplus
}
#endif

#endif // WEB_SERVER_H
