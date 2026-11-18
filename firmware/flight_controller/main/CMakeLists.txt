idf_component_register(
    SRCS
        "main.c"
        "imu_monitor.c"
    INCLUDE_DIRS "."
    REQUIRES
        command_protocol
        wifi_manager
        udp_transport
        usb_transport
        esp_driver_i2c
        esp_timer
)