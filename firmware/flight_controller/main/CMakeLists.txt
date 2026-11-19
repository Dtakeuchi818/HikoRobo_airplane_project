idf_component_register(
    SRCS
        "main.c"
        "imu_monitor.c"
        "imu_attitude.c"
        "sbus_monitor.c"
        "sbus_transform.c"
    INCLUDE_DIRS "."
    REQUIRES
        command_protocol
        wifi_manager
        udp_transport
        usb_transport
        esp_driver_i2c
        esp_driver_uart
        esp_driver_usb_serial_jtag
        nvs_flash
        lwip
        esp_hw_support
        esp_timer
)

set(sbus_defaults_file "${CMAKE_CURRENT_SOURCE_DIR}/../sbus_defaults.json")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${sbus_defaults_file}")
include(sbus_defaults.cmake)
sbus_generate_defaults("${sbus_defaults_file}" "${CMAKE_CURRENT_BINARY_DIR}/sbus_defaults.inc")
target_include_directories(${COMPONENT_LIB} PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")
