#include "net.h"
#include "net_upnp.h"
#include "app.h"
#include "tmf8829/tmf8829.h"

#include <cstring>
#include <cstdio>

#include "esp_log.h"
#include "esp_eth.h"
#include "esp_netif.h"
#include "esp_event.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "net";

static bool s_connected = false;
static bool s_initialized = false;
static esp_eth_handle_t s_eth_handle = nullptr;
static esp_netif_t *s_eth_netif = nullptr;
static char s_device_ip_str[32] = "0.0.0.0";

static void eth_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data) {
    if (event_base == ETH_EVENT) {
        switch (event_id) {
            case ETHERNET_EVENT_CONNECTED:
                ESP_LOGI(TAG, "Ethernet Link Up");
                break;
            case ETHERNET_EVENT_DISCONNECTED:
                ESP_LOGW(TAG, "Ethernet Link Down: device lost network link");
                s_connected = false;
                stop_upnp_broadcast();
                break;
            case ETHERNET_EVENT_START:
                ESP_LOGI(TAG, "Ethernet Started");
                break;
            case ETHERNET_EVENT_STOP:
                ESP_LOGW(TAG, "Ethernet Stopped: device lost network link");
                s_connected = false;
                stop_upnp_broadcast();
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
                start_upnp_broadcast();
                break;
            }
            case IP_EVENT_ETH_LOST_IP:
                ESP_LOGW(TAG, "Network link lost: device lost IP address");
                s_connected = false;
                stop_upnp_broadcast();
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
    ESP_LOGI(TAG, "Ethernet interface initialized.");
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

const char *get_device_ip_str(void) {
    return s_device_ip_str;
}
