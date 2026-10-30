#include "web_server.h"
#include "web/index_html.h"

#include "esp_log.h"
#include "esp_http_server.h"

static const char *TAG = "web_server";

static httpd_handle_t s_server = nullptr;

static esp_err_t index_get_handler(httpd_req_t *req) {
    ESP_LOGI(TAG, "Serving %s (%lu bytes)", req->uri, index_html_length);
    httpd_resp_set_type(req, "text/html");
    return httpd_resp_send(req, reinterpret_cast<const char *>(index_html), index_html_length);
}

static const httpd_uri_t uri_root = {
    .uri       = "/",
    .method    = HTTP_GET,
    .handler   = index_get_handler,
    .user_ctx  = nullptr
};

static const httpd_uri_t uri_index_html = {
    .uri       = "/index.html",
    .method    = HTTP_GET,
    .handler   = index_get_handler,
    .user_ctx  = nullptr
};

esp_err_t start_webserver(void) {
    if (s_server != nullptr) {
        return ESP_OK;
    }

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.lru_purge_enable = true;

    ESP_LOGI(TAG, "Starting HTTP server on port %d...", config.server_port);
    esp_err_t ret = httpd_start(&s_server, &config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start HTTP server: %s", esp_err_to_name(ret));
        return ret;
    }

    httpd_register_uri_handler(s_server, &uri_root);
    httpd_register_uri_handler(s_server, &uri_index_html);

    ESP_LOGI(TAG, "HTTP server started and listening for '/' and '/index.html'");
    return ESP_OK;
}

void stop_webserver(void) {
    if (s_server != nullptr) {
        ESP_LOGI(TAG, "Stopping HTTP server...");
        httpd_stop(s_server);
        s_server = nullptr;
    }
}
