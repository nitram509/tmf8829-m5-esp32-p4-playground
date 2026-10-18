#include "net_handler.h"
#include "net.h"
#include "app.h"
#include "tmf8829/tmf8829.h"
#include "protobuf/message.pb.h"
#include "pb_encode.h"
#include "pb_decode.h"

#include <cstring>
#include <cstdio>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"

static const char *TAG = "net_handler";

static TaskHandle_t s_net_handler_task_handle = nullptr;
static int s_server_sock = -1;
static bool s_running = false;

static bool encode_string_callback(pb_ostream_t *stream, const pb_field_t *field, void * const *arg) {
    const char *str = static_cast<const char *>(*arg);
    if (str == nullptr) {
        str = "";
    }
    if (!pb_encode_tag_for_field(stream, field)) {
        return false;
    }
    return pb_encode_string(stream, reinterpret_cast<const pb_byte_t *>(str), strlen(str));
}

static void handle_get_device_info(int sock, const struct sockaddr_in &source_addr) {
    if (!is_network_connected()) {
        ESP_LOGW(TAG, "Cannot send DeviceInfo UDP response: network not connected or no IP assigned");
        return;
    }

    char fw_ver[32] = {0};
    char chip_ver[32] = {0};
    char chip_serial[32] = {0};

    snprintf(fw_ver, sizeof(fw_ver), "%u.%u.%u.%u",
             tmf8829.device.appVersion[0],
             tmf8829.device.appVersion[1],
             tmf8829.device.appVersion[2],
             tmf8829.device.appVersion[3]);

    snprintf(chip_ver, sizeof(chip_ver), "%u.%u",
             tmf8829.device.chipVersion[0],
             tmf8829.device.chipVersion[1]);

    snprintf(chip_serial, sizeof(chip_serial), "0x%lX",
             static_cast<unsigned long>(tmf8829.device.deviceSerialNumber));

    DeviceInfo response = DeviceInfo_init_default;
    response.Firmware_Version.funcs.encode = &encode_string_callback;
    response.Firmware_Version.arg = fw_ver;

    response.Chip_Version.funcs.encode = &encode_string_callback;
    response.Chip_Version.arg = chip_ver;

    response.Chip_Serial.funcs.encode = &encode_string_callback;
    response.Chip_Serial.arg = chip_serial;

    uint8_t tx_buffer[256];
    pb_ostream_t out_stream = pb_ostream_from_buffer(reinterpret_cast<pb_byte_t *>(tx_buffer), sizeof(tx_buffer));

    if (pb_encode(&out_stream, DeviceInfo_fields, &response)) {
        ssize_t sent = sendto(sock, tx_buffer, out_stream.bytes_written, 0,
                              reinterpret_cast<const struct sockaddr *>(&source_addr), sizeof(source_addr));
        if (sent < 0) {
            ESP_LOGE(TAG, "Failed to send DeviceInfo UDP response: errno %d", errno);
        } else {
            ESP_LOGI(TAG, "Sent DeviceInfo response (%d bytes) to %s:%d [FW: %s, Chip: %s, Serial: %s]",
                     static_cast<int>(sent),
                     inet_ntoa(source_addr.sin_addr),
                     ntohs(source_addr.sin_port),
                     fw_ver, chip_ver, chip_serial);
        }
    } else {
        ESP_LOGE(TAG, "Failed to encode DeviceInfo protobuf: %s", PB_GET_ERROR(&out_stream));
    }
}

static void handle_request_packet(int sock, const uint8_t *buffer, size_t len, const struct sockaddr_in &source_addr) {
    Request request = Request_init_zero;
    pb_istream_t in_stream = pb_istream_from_buffer(reinterpret_cast<const pb_byte_t *>(buffer), len);

    if (!pb_decode(&in_stream, Request_fields, &request)) {
        ESP_LOGW(TAG, "Failed to decode Request message from %s:%d (error: %s)",
                 inet_ntoa(source_addr.sin_addr), ntohs(source_addr.sin_port), PB_GET_ERROR(&in_stream));
        return;
    }

    ESP_LOGI(TAG, "Received Request with command=%d from %s:%d",
             static_cast<int>(request.command), inet_ntoa(source_addr.sin_addr), ntohs(source_addr.sin_port));

    switch (request.command) {
        case Command_GetDeviceInfo:
            handle_get_device_info(sock, source_addr);
            break;
        default:
            ESP_LOGW(TAG, "Unhandled command ID: %d", static_cast<int>(request.command));
            break;
    }
}

static void net_handler_task(void *pvParameters) {
    ESP_LOGI(TAG, "Starting UDP listener task on port %d...", CONFIG_TMF8829_UDP_TARGET_PORT);

    s_server_sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
    if (s_server_sock < 0) {
        ESP_LOGE(TAG, "Unable to create UDP listener socket: errno %d", errno);
        s_running = false;
        vTaskDelete(nullptr);
        return;
    }

    sockaddr_in bind_addr{};
    memset(&bind_addr, 0, sizeof(bind_addr));
    bind_addr.sin_family = AF_INET;
    bind_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    bind_addr.sin_port = htons(CONFIG_TMF8829_UDP_TARGET_PORT);

    int err = bind(s_server_sock, reinterpret_cast<struct sockaddr *>(&bind_addr), sizeof(bind_addr));
    if (err < 0) {
        ESP_LOGE(TAG, "Socket unable to bind to port %d: errno %d", CONFIG_TMF8829_UDP_TARGET_PORT, errno);
        close(s_server_sock);
        s_server_sock = -1;
        s_running = false;
        vTaskDelete(nullptr);
        return;
    }

    ESP_LOGI(TAG, "UDP listener successfully bound to port %d. Waiting for incoming requests...", CONFIG_TMF8829_UDP_TARGET_PORT);
    s_running = true;

    uint8_t rx_buffer[512];

    while (s_running) {
        sockaddr_in source_addr{};
        socklen_t socklen = sizeof(source_addr);
        ssize_t len = recvfrom(s_server_sock, rx_buffer, sizeof(rx_buffer), 0,
                               reinterpret_cast<struct sockaddr *>(&source_addr), &socklen);

        if (len < 0) {
            if (s_running) {
                ESP_LOGE(TAG, "recvfrom failed: errno %d", errno);
            }
            break;
        }

        if (len > 0) {
            handle_request_packet(s_server_sock, rx_buffer, static_cast<size_t>(len), source_addr);
        }
    }

    if (s_server_sock >= 0) {
        close(s_server_sock);
        s_server_sock = -1;
    }
    s_running = false;
    vTaskDelete(nullptr);
}

esp_err_t start_net_handler(void) {
    if (s_running || s_net_handler_task_handle != nullptr) {
        return ESP_OK;
    }

    BaseType_t ret = xTaskCreate(net_handler_task, "net_handler_task", 4096, nullptr, 5, &s_net_handler_task_handle);
    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create net_handler FreeRTOS task");
        return ESP_FAIL;
    }
    return ESP_OK;
}

void stop_net_handler(void) {
    s_running = false;
    if (s_server_sock >= 0) {
        shutdown(s_server_sock, SHUT_RDWR);
        close(s_server_sock);
        s_server_sock = -1;
    }
    s_net_handler_task_handle = nullptr;
}
