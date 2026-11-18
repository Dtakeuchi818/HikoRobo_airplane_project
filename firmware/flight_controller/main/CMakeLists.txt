idf_component_register(
    SRCS
        "main.c"
        "imu_monitor.c"
        "imu_attitude.c"
    INCLUDE_DIRS "."
    REQUIRES
        command_protocol
        wifi_manager
        udp_transport
        usb_transport
        esp_driver_i2c
        lwip
        esp_hw_support
        esp_timer
)