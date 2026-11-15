idf_component_register(
    SRCS "usb_transport.c"
    INCLUDE_DIRS "include"
    PRIV_REQUIRES command_protocol esp_driver_usb_serial_jtag
)
