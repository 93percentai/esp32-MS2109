idf_component_register(SRCS dns_server.c dns_packet.c
                       INCLUDE_DIRS include
                       PRIV_REQUIRES esp_netif)
