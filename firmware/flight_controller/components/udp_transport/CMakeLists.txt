idf_component_register(
    SRCS "udp_transport.c"
    INCLUDE_DIRS "include"
    PRIV_REQUIRES command_protocol wifi_manager lwip
)
