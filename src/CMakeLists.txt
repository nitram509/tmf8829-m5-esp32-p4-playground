idf_component_register(SRCS "main.cpp"
                            "app.cpp"
                            "image_pgm.cpp"
                            "help.cpp"
                            "net.cpp"
                            "net_handler.cpp"
                            "web_server.cpp"
                            "tmf8829/tmf8829_firmware.c"
                            "tmf8829/tmf8829.c"
                            "tmf8829/tmf8829_shim.cpp"
                            "protobuf/message.pb.c"
                            "vendor/nanopb-0.4.9.2/pb_common.c"
                            "vendor/nanopb-0.4.9.2/pb_decode.c"
                            "vendor/nanopb-0.4.9.2/pb_encode.c"
                       INCLUDE_DIRS "." "tmf8829" "protobuf" "vendor/nanopb-0.4.9.2" "web" "${CMAKE_CURRENT_BINARY_DIR}"
                       REQUIRES driver esp_driver_gpio esp_driver_spi esp_timer esp_rom freertos log esp_eth esp_netif esp_event lwip esp_http_server)

# 1. Locate the srec_cat tool
find_program(SREC_CAT_BIN srec_cat REQUIRED)

# 2. Define the list of web asset source files
set(WEB_ASSETS
        "web/index.html"
)

# 3. Iterate through each file and generate custom build rules
foreach(ASSET_PATH ${WEB_ASSETS})
    get_filename_component(ASSET_ABS "${CMAKE_CURRENT_SOURCE_DIR}/${ASSET_PATH}" ABSOLUTE)
    get_filename_component(ASSET_NAME "${ASSET_PATH}" NAME)

    string(REGEX REPLACE "[^a-zA-Z0-9_]" "_" C_VAR_NAME "${ASSET_NAME}")
    set(OUT_C "${CMAKE_CURRENT_BINARY_DIR}/${C_VAR_NAME}.c")

    add_custom_command(
            OUTPUT "${OUT_C}"
            COMMAND ${SREC_CAT_BIN} "${ASSET_ABS}" -binary
            -output "${OUT_C}" -C-Array "${C_VAR_NAME}"
            DEPENDS "${ASSET_ABS}"
            WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
            COMMENT "Converting ${ASSET_NAME} to ${C_VAR_NAME}.c via srec_cat..."
            VERBATIM
    )
    target_sources(${COMPONENT_LIB} PRIVATE "${OUT_C}")
endforeach()
