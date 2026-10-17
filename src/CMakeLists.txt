idf_component_register(SRCS "main.cpp"
                            "app.cpp"
                            "tmf8829/tmf8829_firmware.c"
                            "tmf8829/tmf8829.c"
                            "tmf8829/tmf8829_shim.cpp"
                            "tmf8829/tmf8829_help.cpp"
                       INCLUDE_DIRS "." "tmf8829"
                       REQUIRES driver esp_driver_gpio esp_driver_spi esp_timer esp_rom freertos log)
