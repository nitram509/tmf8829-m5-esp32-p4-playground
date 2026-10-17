#include "net.h"

#include <cstring>
#include <sys/socket.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <unistd.h>

#include "esp_log.h"
#include "esp_eth.h"
#include "esp_netif.h"
#include "esp_event.h"
#include "sdkconfig.h"

static const char *TAG = "net";

static bool s_connected = false;
static bool s_initialized = false;
static esp_eth_handle_t s_eth_handle = nullptr;
static esp_netif_t *s_eth_netif = nullptr;

static void eth_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data) {
    if (event_base == ETH_EVENT) {
        switch (event_id) {
            case ETHERNET_EVENT_CONNECTED:
                ESP_LOGI(TAG, "Ethernet Link Up");
                break;
            case ETHERNET_EVENT_DISCONNECTED:
                ESP_LOGI(TAG, "Ethernet Link Down");
                s_connected = false;
                break;
            case ETHERNET_EVENT_START:
                ESP_LOGI(TAG, "Ethernet Started");
                break;
            case ETHERNET_EVENT_STOP:
                ESP_LOGI(TAG, "Ethernet Stopped");
                s_connected = false;
                break;
            default:
                break;
        }
    } else if (event_base == IP_EVENT) {
        if (event_id == IP_EVENT_ETH_GOT_IP) {
            auto *event = static_cast<ip_event_got_ip_t *>(event_data);
            ESP_LOGI(TAG, "Ethernet Got IP Address: " IPSTR, IP2STR(&event->ip_info.ip));
            ESP_LOGI(TAG, "Netmask: " IPSTR ", Gateway: " IPSTR, IP2STR(&event->ip_info.netmask),
                     IP2STR(&event->ip_info.gw));
            s_connected = true;
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
    esp_event_handler_register(IP_EVENT, IP_EVENT_ETH_GOT_IP, &eth_event_handler, nullptr);

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
    return s_connected;
}

esp_err_t send_raw_frame_udp(const uint8_t *frameData, size_t length) {
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
