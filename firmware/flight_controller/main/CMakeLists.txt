idf_component_register(
    SRCS "main.c"
    PRIV_REQUIRES command_protocol usb_transport wifi_manager udp_transport
)
