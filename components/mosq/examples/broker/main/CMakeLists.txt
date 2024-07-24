idf_component_register(SRCS "broker.c"
                       PRIV_REQUIRES nvs_flash esp_netif esp_event mqtt)
