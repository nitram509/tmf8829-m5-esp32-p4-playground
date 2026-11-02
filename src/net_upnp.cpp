#include "net_upnp.h"
#include "net.h"
#include "app.h"
#include "tmf8829/tmf8829.h"

#include <cstring>
#include <cstdio>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>

#include "esp_log.h"
#include "esp_mac.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static auto TAG = "net_upnp";

#define SSDP_MULTICAST_IP   "239.255.255.250"
#define SSDP_PORT           1900
#define UPNP_BROADCAST_INTERVAL_MS 5000

static TaskHandle_t s_upnp_task_handle = nullptr;
static bool s_upnp_running = false;

static void get_sensor_serial_str(char *buf, const size_t max_len) {
    snprintf(buf, max_len, "0x%08lX",
             static_cast<unsigned long>(tmf8829.device.deviceSerialNumber));
}

static void get_device_mac(uint8_t mac[6]) {
    if (esp_efuse_mac_get_default(mac) != ESP_OK) {
        esp_read_mac(mac, ESP_MAC_ETH);
    }
}

void get_device_uuid_str(char *buf, const size_t max_len, const uint8_t *mac) {
    snprintf(buf, max_len,
             "%02x%02x%02x%02x-%02x%02x-0000-0000-%02x%02x%02x%02x%02x%02x",
             mac[0], mac[1], mac[2], mac[3],
             mac[4], mac[5],
             // ensure the last 6 bytes are easy to parse, when retrieving the MAC
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}

static void format_upnp_alive_message(char *buffer, const size_t max_len, const char *ip_str,
                                      const uint8_t *mac) {
    char uuid_str[40];
    get_device_uuid_str(uuid_str, sizeof(uuid_str), mac);

    snprintf(buffer, max_len,
             "NOTIFY * HTTP/1.1\r\n"
             "HOST: %s:%d\r\n"
             "CACHE-CONTROL: max-age=1800\r\n"
             "LOCATION: http://%s:80/description.xml\r\n"
             "NT: %s\r\n"
             "NTS: ssdp:alive\r\n"
             "USN: uuid:%s::%s\r\n"
             "\r\n",
             SSDP_MULTICAST_IP, SSDP_PORT,
             ip_str,
             UPNP_DEVICE_TYPE,
             uuid_str, UPNP_DEVICE_TYPE);
}

static void format_upnp_byebye_message(char *buffer, const size_t max_len, const uint8_t *mac) {
    char uuid_str[40];
    get_device_uuid_str(uuid_str, sizeof(uuid_str), mac);

    snprintf(buffer, max_len,
             "NOTIFY * HTTP/1.1\r\n"
             "HOST: %s:%d\r\n"
             "NT: %s\r\n"
             "NTS: ssdp:byebye\r\n"
             "USN: uuid:%s::%s\r\n"
             "\r\n",
             SSDP_MULTICAST_IP, SSDP_PORT,
             UPNP_DEVICE_TYPE,
             uuid_str, UPNP_DEVICE_TYPE);
}

int generate_upnp_device_description_xml(char *buffer, const size_t max_len) {
    uint8_t mac[6] = {};
    if (esp_efuse_mac_get_default(mac) != ESP_OK) {
        esp_read_mac(mac, ESP_MAC_ETH);
    }
    char uuid_str[40];
    get_device_uuid_str(uuid_str, sizeof(uuid_str), mac);

    char sensor_serial[32] = {};
    get_sensor_serial_str(sensor_serial, sizeof(sensor_serial));

    const int len = snprintf(buffer, max_len,
                             "<?xml version=\"1.0\"?>\r\n"
                             "<root xmlns=\"urn:schemas-upnp-org:device-1-0\">\r\n"
                             " <specVersion>\r\n"
                             "  <major>1</major>\r\n"
                             "  <minor>0</minor>\r\n"
                             " </specVersion>\r\n"
                             " <device>\r\n"
                             "  <deviceType>%s</deviceType>\r\n"
                             "  <friendlyName>%s</friendlyName>\r\n"
                             "  <manufacturer>Nitram509</manufacturer>\r\n"
                             "  <modelName>ESP32-P4-TMF8829</modelName>\r\n"
                             "  <modelNumber>1</modelNumber>\r\n"
                             "  <serialNumber>%s</serialNumber>\r\n"
                             "  <UDN>uuid:%s</UDN>\r\n"
                             "  <presentationURL>/</presentationURL>\r\n"
                             " </device>\r\n"
                             "</root>\r\n",
                             UPNP_DEVICE_TYPE,
                             UPNP_DEVICE_NAME,
                             sensor_serial,
                             uuid_str);
    return len;
}

static void send_upnp_udp_packet(const char *payload) {
    const int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
    if (sock < 0) {
        ESP_LOGE(TAG, "Failed to create UPnP UDP socket: errno %d", errno);
        return;
    }

    constexpr int broadcast_en = 1;
    setsockopt(sock, SOL_SOCKET, SO_BROADCAST, &broadcast_en, sizeof(broadcast_en));

    constexpr uint8_t ttl = 4;
    setsockopt(sock, IPPROTO_IP, IP_MULTICAST_TTL, &ttl, sizeof(ttl));

    constexpr timeval timeout = {.tv_sec = 2, .tv_usec = 0};
    setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));

    const size_t payload_len = strlen(payload);

    // 1. Send to SSDP Multicast address (239.255.255.250:1900)
    sockaddr_in mcast_addr{};
    mcast_addr.sin_family = AF_INET;
    mcast_addr.sin_port = htons(SSDP_PORT);
    inet_pton(AF_INET, SSDP_MULTICAST_IP, &mcast_addr.sin_addr);

    const ssize_t mcast_sent = sendto(sock, payload, payload_len, 0,
                                      reinterpret_cast<struct sockaddr *>(&mcast_addr), sizeof(mcast_addr));

    // 2. Send to Subnet Broadcast address (255.255.255.255:1900)
    sockaddr_in bcast_addr{};
    bcast_addr.sin_family = AF_INET;
    bcast_addr.sin_port = htons(SSDP_PORT);
    bcast_addr.sin_addr.s_addr = htonl(INADDR_BROADCAST);

    const ssize_t bcast_sent = sendto(sock, payload, payload_len, 0,
                                      reinterpret_cast<struct sockaddr *>(&bcast_addr), sizeof(bcast_addr));

    if (mcast_sent < 0 && bcast_sent < 0) {
        ESP_LOGE(TAG, "Failed to send UPnP UDP packet: errno %d", errno);
    }

    close(sock);
}

static void upnp_broadcast_task(void *pvParameters) {
    ESP_LOGI(TAG, "UPnP broadcast task started (5s broadcast interval)");

    char notify_buf[512];
    char sensor_serial[32];
    uint8_t mac[6];

    while (s_upnp_running) {
        if (is_network_connected()) {
            const char *ip_str = get_device_ip_str();
            get_sensor_serial_str(sensor_serial, sizeof(sensor_serial));
            get_device_mac(mac);

            format_upnp_alive_message(notify_buf, sizeof(notify_buf), ip_str,
                                      mac);

            send_upnp_udp_packet(notify_buf);

            ESP_LOGD(TAG, "Sent UPnP NOTIFY alive [IP: %s, Device: %s, Serial: %s]",
                     ip_str, UPNP_DEVICE_NAME, sensor_serial);
        }

        vTaskDelay(pdMS_TO_TICKS(UPNP_BROADCAST_INTERVAL_MS));
    }

    // Send ssdp:byebye on shutdown if network was available
    if (is_network_connected()) {
        get_sensor_serial_str(sensor_serial, sizeof(sensor_serial));
        get_device_mac(mac);
        format_upnp_byebye_message(notify_buf, sizeof(notify_buf),
                                   mac);
        send_upnp_udp_packet(notify_buf);
        ESP_LOGD(TAG, "Sent UPnP NOTIFY byebye");
    }

    s_upnp_task_handle = nullptr;
    vTaskDelete(nullptr);
}

esp_err_t start_upnp_broadcast() {
    if (s_upnp_running || s_upnp_task_handle != nullptr) {
        return ESP_OK;
    }

    s_upnp_running = true;
    if (const BaseType_t ret = xTaskCreate(upnp_broadcast_task, "net_upnp_task", 4096, nullptr, 5, &s_upnp_task_handle);
        ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create net_upnp FreeRTOS task");
        s_upnp_running = false;
        return ESP_FAIL;
    }

    ESP_LOGD(TAG, "UPnP broadcast service started");
    return ESP_OK;
}

void stop_upnp_broadcast() {
    if (!s_upnp_running) {
        return;
    }

    ESP_LOGI(TAG, "Stopping UPnP broadcast service...");
    s_upnp_running = false;
}

bool is_upnp_broadcast_running() {
    return s_upnp_running;
}
