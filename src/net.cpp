#include "net.h"
#include "app.h"
#include "tmf8829/tmf8829.h"

#include <cstring>
#include <cstdio>
#include <sys/socket.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <unistd.h>

#include "esp_log.h"
#include "esp_eth.h"
#include "esp_netif.h"
#include "esp_event.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"

static const char *TAG = "net";

static bool s_connected = false;
static bool s_initialized = false;
static esp_eth_handle_t s_eth_handle = nullptr;
static esp_netif_t *s_eth_netif = nullptr;

static TaskHandle_t s_beacon_task_handle = nullptr;
static bool s_beacon_running = false;
static char s_device_ip_str[32] = "0.0.0.0";

static void format_device_info_string(char *buffer, size_t max_len, const char *ip_str) {
    const char *state_str = "unknown";
    switch (stateTmf8829) {
        case TMF8829_STATE_DISABLED: state_str = "disabled"; break;
        case TMF8829_STATE_STANDBY:  state_str = "standby";  break;
        case TMF8829_STATE_STOPPED:  state_str = "stopped";  break;
        case TMF8829_STATE_MEASURE:  state_str = "measure";  break;
        case TMF8829_STATE_ERROR:    state_str = "error";    break;
        default:                     state_str = "???";      break;
    }

    snprintf(buffer, max_len,
             "Device IP: %s\n"
             "Driver Version: %u.%u.5\n"
             "Firmware Version: %u.%u.%u.%u\n"
             "Chip Version: %u.%u\n"
             "Serial Number: 0x%lX\n"
             "State: %s\n",
             ip_str,
             tmf8829.info.version[0], tmf8829.info.version[1],
             tmf8829.device.appVersion[0], tmf8829.device.appVersion[1],
             tmf8829.device.appVersion[2], tmf8829.device.appVersion[3],
             tmf8829.device.chipVersion[0], tmf8829.device.chipVersion[1],
             static_cast<unsigned long>(tmf8829.device.deviceSerialNumber),
             state_str);
}

static void device_info_beacon_task(void *pvParameters) {
    ESP_LOGI(TAG, "Device info beacon task started. Target: %s:%d (first 3 beacons at 5s interval, then every 60s)...",
             CONFIG_TMF8829_UDP_TARGET_HOST, CONFIG_TMF8829_UDP_TARGET_PORT);

    char msg_buf[384];
    uint32_t send_count = 0;

    while (s_beacon_running) {
        if (is_network_connected()) {
            format_device_info_string(msg_buf, sizeof(msg_buf), s_device_ip_str);

            int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
            if (sock >= 0) {
                struct timeval timeout = {
                    .tv_sec = 2,
                    .tv_usec = 0,
                };
                setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));

                struct sockaddr_in dest_addr;
                memset(&dest_addr, 0, sizeof(dest_addr));
                dest_addr.sin_family = AF_INET;
                dest_addr.sin_port = htons(CONFIG_TMF8829_UDP_TARGET_PORT);

                int res = inet_pton(AF_INET, CONFIG_TMF8829_UDP_TARGET_HOST, &dest_addr.sin_addr);
                if (res <= 0) {
                    const struct hostent *he = gethostbyname(CONFIG_TMF8829_UDP_TARGET_HOST);
                    if (he != nullptr) {
                        memcpy(&dest_addr.sin_addr, he->h_addr_list[0], sizeof(struct in_addr));
                        res = 1;
                    }
                }

                if (res > 0) {
                    ssize_t sent = sendto(sock, msg_buf, strlen(msg_buf), 0,
                                          reinterpret_cast<struct sockaddr *>(&dest_addr), sizeof(dest_addr));
                    if (sent >= 0) {
                        send_count++;
                        uint32_t next_delay_sec = (send_count < 3) ? 5 : 60;
                        ESP_LOGI(TAG, "Sent beacon #%lu (%d bytes) to %s:%d (next in %lus) [IP: %s, FW: %u.%u.%u.%u, Chip: %u.%u, Serial: 0x%lX]",
                                 static_cast<unsigned long>(send_count),
                                 static_cast<int>(sent),
                                 CONFIG_TMF8829_UDP_TARGET_HOST, CONFIG_TMF8829_UDP_TARGET_PORT,
                                 static_cast<unsigned long>(next_delay_sec),
                                 s_device_ip_str,
                                 tmf8829.device.appVersion[0], tmf8829.device.appVersion[1],
                                 tmf8829.device.appVersion[2], tmf8829.device.appVersion[3],
                                 tmf8829.device.chipVersion[0], tmf8829.device.chipVersion[1],
                                 static_cast<unsigned long>(tmf8829.device.deviceSerialNumber));
                    } else {
                        ESP_LOGE(TAG, "Failed to send beacon UDP packet: errno %d", errno);
                    }
                } else {
                    ESP_LOGE(TAG, "Failed to resolve beacon target host: %s", CONFIG_TMF8829_UDP_TARGET_HOST);
                }

                close(sock);
            } else {
                ESP_LOGE(TAG, "Unable to create beacon socket: errno %d", errno);
            }
        }

        uint32_t delay_ms = (send_count < 3) ? 5000 : 60000;
        vTaskDelay(pdMS_TO_TICKS(delay_ms));
    }

    s_beacon_task_handle = nullptr;
    vTaskDelete(nullptr);
}

esp_err_t start_device_info_beacon(void) {
    if (s_beacon_running || s_beacon_task_handle != nullptr) {
        return ESP_OK;
    }

    s_beacon_running = true;
    BaseType_t ret = xTaskCreate(device_info_beacon_task, "dev_info_beacon", 4096, nullptr, 5, &s_beacon_task_handle);
    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create device_info_beacon FreeRTOS task");
        s_beacon_running = false;
        return ESP_FAIL;
    }
    return ESP_OK;
}

void stop_device_info_beacon(void) {
    s_beacon_running = false;
}

static void eth_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data) {
    if (event_base == ETH_EVENT) {
        switch (event_id) {
            case ETHERNET_EVENT_CONNECTED:
                ESP_LOGI(TAG, "Ethernet Link Up");
                break;
            case ETHERNET_EVENT_DISCONNECTED:
                ESP_LOGW(TAG, "Ethernet Link Down: device lost network link");
                s_connected = false;
                stop_device_info_beacon();
                break;
            case ETHERNET_EVENT_START:
                ESP_LOGI(TAG, "Ethernet Started");
                break;
            case ETHERNET_EVENT_STOP:
                ESP_LOGW(TAG, "Ethernet Stopped: device lost network link");
                s_connected = false;
                stop_device_info_beacon();
                break;
            default:
                break;
        }
    } else if (event_base == IP_EVENT) {
        switch (event_id) {
            case IP_EVENT_ETH_GOT_IP: {
                auto *event = static_cast<ip_event_got_ip_t *>(event_data);
                snprintf(s_device_ip_str, sizeof(s_device_ip_str), IPSTR, IP2STR(&event->ip_info.ip));
                ESP_LOGI(TAG, "Network enabled. Device IP Address: %s", s_device_ip_str);
                ESP_LOGI(TAG, "Netmask: " IPSTR ", Gateway: " IPSTR, IP2STR(&event->ip_info.netmask),
                         IP2STR(&event->ip_info.gw));
                s_connected = true;
                start_device_info_beacon();
                break;
            }
            case IP_EVENT_ETH_LOST_IP:
                ESP_LOGW(TAG, "Network link lost: device lost IP address");
                s_connected = false;
                stop_device_info_beacon();
                break;
            default:
                break;
        }
    }
}

esp_err_t init_network() {
    if (s_initialized) {
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Initializing TCP/IP network stack...");

    esp_err_t err = esp_netif_init();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "Failed to initialize esp_netif: %s", esp_err_to_name(err));
        return err;
    }

    err = esp_event_loop_create_default();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "Failed to create default event loop: %s", esp_err_to_name(err));
        return err;
    }

    esp_netif_config_t netif_cfg = ESP_NETIF_DEFAULT_ETH();
    s_eth_netif = esp_netif_new(&netif_cfg);
    if (s_eth_netif == nullptr) {
        ESP_LOGE(TAG, "Failed to create esp_netif object for Ethernet");
        return ESP_FAIL;
    }

    eth_esp32_emac_config_t emac_config = ETH_ESP32_EMAC_DEFAULT_CONFIG();
    eth_mac_config_t mac_config = ETH_MAC_DEFAULT_CONFIG();
    esp_eth_mac_t *mac = esp_eth_mac_new_esp32(&emac_config, &mac_config);
    if (mac == nullptr) {
        ESP_LOGE(TAG, "Failed to create EMAC instance");
        return ESP_FAIL;
    }

    eth_phy_config_t phy_config = ETH_PHY_DEFAULT_CONFIG();
    phy_config.phy_addr = ESP_ETH_PHY_ADDR_AUTO;
    phy_config.reset_gpio_num = -1;
    esp_eth_phy_t *phy = esp_eth_phy_new_ip101(&phy_config);
    if (phy == nullptr) {
        ESP_LOGE(TAG, "Failed to create IP101 PHY instance");
        mac->del(mac);
        return ESP_FAIL;
    }

    esp_eth_config_t eth_config = ETH_DEFAULT_CONFIG(mac, phy);
    err = esp_eth_driver_install(&eth_config, &s_eth_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to install Ethernet driver: %s", esp_err_to_name(err));
        mac->del(mac);
        phy->del(phy);
        return err;
    }

    err = esp_netif_attach(s_eth_netif, esp_eth_new_netif_glue(s_eth_handle));
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to attach netif glue: %s", esp_err_to_name(err));
        return err;
    }

    esp_event_handler_register(ETH_EVENT, ESP_EVENT_ANY_ID, &eth_event_handler, nullptr);
    esp_event_handler_register(IP_EVENT, ESP_EVENT_ANY_ID, &eth_event_handler, nullptr);

    err = esp_eth_start(s_eth_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start Ethernet driver: %s", esp_err_to_name(err));
        return err;
    }

    s_initialized = true;
    ESP_LOGI(TAG, "Ethernet interface initialized. Target UDP configured as %s:%d",
             CONFIG_TMF8829_UDP_TARGET_HOST, CONFIG_TMF8829_UDP_TARGET_PORT);
    return ESP_OK;
}

bool is_network_connected() {
    if (!s_connected || s_eth_netif == nullptr) {
        return false;
    }
    esp_netif_ip_info_t ip_info;
    if (esp_netif_get_ip_info(s_eth_netif, &ip_info) == ESP_OK) {
        return (ip_info.ip.addr != 0);
    }
    return false;
}

esp_err_t send_raw_frame_udp(const uint8_t *frameData, size_t length) {
    if (!is_network_connected()) {
        ESP_LOGW(TAG, "Cannot send raw frame via UDP: network not connected or no IP assigned");
        return ESP_ERR_INVALID_STATE;
    }

    if (frameData == nullptr || length == 0) {
        ESP_LOGE(TAG, "Cannot send UDP packet: invalid buffer or length 0");
        return ESP_ERR_INVALID_ARG;
    }

    int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
    if (sock < 0) {
        ESP_LOGE(TAG, "Unable to create UDP socket: errno %d", errno);
        return ESP_FAIL;
    }

    timeval timeout = {
        .tv_sec = 2,
        .tv_usec = 0,
    };
    setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));

    sockaddr_in dest_addr{};
    memset(&dest_addr, 0, sizeof(dest_addr));
    dest_addr.sin_family = AF_INET;
    dest_addr.sin_port = htons(CONFIG_TMF8829_UDP_TARGET_PORT);

    if (const int res = inet_pton(AF_INET, CONFIG_TMF8829_UDP_TARGET_HOST, &dest_addr.sin_addr); res <= 0) {
        const hostent *he = gethostbyname(CONFIG_TMF8829_UDP_TARGET_HOST);
        if (he == nullptr) {
            ESP_LOGE(TAG, "Unable to resolve target host: %s", CONFIG_TMF8829_UDP_TARGET_HOST);
            close(sock);
            return ESP_FAIL;
        }
        memcpy(&dest_addr.sin_addr, he->h_addr_list[0], sizeof(in_addr));
    }

    ESP_LOGI(TAG, "Sending %u raw frame bytes via UDP to %s:%d...",
             static_cast<unsigned int>(length), CONFIG_TMF8829_UDP_TARGET_HOST, CONFIG_TMF8829_UDP_TARGET_PORT);

    const ssize_t sent = sendto(sock, frameData, length, 0, reinterpret_cast<struct sockaddr *>(&dest_addr),
                          sizeof(dest_addr));
    if (sent < 0) {
        ESP_LOGE(TAG, "Failed to send UDP packet: errno %d", errno);
        close(sock);
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Successfully transmitted %d bytes to %s:%d",
             static_cast<int>(sent), CONFIG_TMF8829_UDP_TARGET_HOST, CONFIG_TMF8829_UDP_TARGET_PORT);

    close(sock);
    return ESP_OK;
}

esp_err_t send_distance_map_pgm_udp(const uint16_t *distances, uint16_t width, uint16_t height) {
    if (!is_network_connected()) {
        ESP_LOGW(TAG, "Cannot send PGM image via UDP: network not connected or no IP assigned");
        return ESP_ERR_INVALID_STATE;
    }

    if (distances == nullptr || width == 0 || height == 0) {
        ESP_LOGE(TAG, "Cannot send PGM UDP packet: invalid parameters");
        return ESP_ERR_INVALID_ARG;
    }

    uint32_t totalPixels = (uint32_t) width * height;
    uint16_t maxDist = 0;
    for (uint32_t i = 0; i < totalPixels; i++) {
        if (distances[i] > maxDist) {
            maxDist = distances[i];
        }
    }
    uint16_t maxVal = (maxDist > 255) ? maxDist : 255;

    int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
    if (sock < 0) {
        ESP_LOGE(TAG, "Unable to create UDP socket: errno %d", errno);
        return ESP_FAIL;
    }

    timeval timeout = {
        .tv_sec = 2,
        .tv_usec = 0,
    };
    setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));

    sockaddr_in dest_addr{};
    memset(&dest_addr, 0, sizeof(dest_addr));
    dest_addr.sin_family = AF_INET;
    dest_addr.sin_port = htons(CONFIG_TMF8829_UDP_TARGET_PORT);

    if (const int res = inet_pton(AF_INET, CONFIG_TMF8829_UDP_TARGET_HOST, &dest_addr.sin_addr); res <= 0) {
        const hostent *he = gethostbyname(CONFIG_TMF8829_UDP_TARGET_HOST);
        if (he == nullptr) {
            ESP_LOGE(TAG, "Unable to resolve target host: %s", CONFIG_TMF8829_UDP_TARGET_HOST);
            close(sock);
            return ESP_FAIL;
        }
        memcpy(&dest_addr.sin_addr, he->h_addr_list[0], sizeof(in_addr));
    }

    ESP_LOGI(TAG, "Sending PGM image (%ux%u, %u lines) via UDP to %s:%d...",
             (unsigned int) width, (unsigned int) height, (unsigned int) height,
             CONFIG_TMF8829_UDP_TARGET_HOST, CONFIG_TMF8829_UDP_TARGET_PORT);

    char line_buf[512];
    size_t total_sent = 0;

    // Send PGM Header as first datagram
    int header_len = snprintf(line_buf, sizeof(line_buf), "P2\n# TMF8829 %ux%u Distance Map\n%u %u\n%u\n",
                              (unsigned int) width, (unsigned int) height,
                              (unsigned int) width, (unsigned int) height,
                              (unsigned int) maxVal);
    if (header_len > 0 && (size_t) header_len < sizeof(line_buf)) {
        ssize_t sent = sendto(sock, line_buf, header_len, 0,
                              reinterpret_cast<struct sockaddr *>(&dest_addr), sizeof(dest_addr));
        if (sent > 0) {
            total_sent += sent;
        }
        vTaskDelay(pdMS_TO_TICKS(5));
    }

    // Send each pixel row as an individual UDP datagram
    for (uint16_t y = 0; y < height; y++) {
        int len = 0;
        for (uint16_t x = 0; x < width; x++) {
            int written = snprintf(line_buf + len, sizeof(line_buf) - len, (x < (width - 1)) ? "%u " : "%u\n",
                                   (unsigned int) distances[y * width + x]);
            if (written > 0 && (size_t) (len + written) < sizeof(line_buf)) {
                len += written;
            } else {
                break;
            }
        }

        if (len > 0) {
            ssize_t sent = sendto(sock, line_buf, len, 0,
                                  reinterpret_cast<struct sockaddr *>(&dest_addr), sizeof(dest_addr));
            if (sent > 0) {
                total_sent += sent;
            } else {
                ESP_LOGW(TAG, "Failed to send row %u UDP datagram: errno %d", y, errno);
            }
            vTaskDelay(pdMS_TO_TICKS(5));
        }
    }

    ESP_LOGI(TAG, "Successfully transmitted complete PGM image (%u lines, %d bytes) to %s:%d",
             (unsigned int) height, static_cast<int>(total_sent),
             CONFIG_TMF8829_UDP_TARGET_HOST, CONFIG_TMF8829_UDP_TARGET_PORT);

    close(sock);
    return ESP_OK;
}
