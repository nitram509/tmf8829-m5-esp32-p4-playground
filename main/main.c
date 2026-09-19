#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"
#include "tmf8829_app.h"

#define UART_BAUD_RATE 2000000

static const char *TAG = "tmf8829_app";

void app_main(void)
{
    ESP_LOGI(TAG, "Starting TMF8829 Arduino application flow");
    initial_setup(1, UART_BAUD_RATE, CONFIG_TMF8829_SPI_CLOCK_HZ);
    while (main_loop()) {
        TickType_t delay_ticks = pdMS_TO_TICKS(10);
        if (delay_ticks == 0) {
            delay_ticks = 1;
        }
        vTaskDelay(delay_ticks);
    }
    final_clean_shutdown();
}
