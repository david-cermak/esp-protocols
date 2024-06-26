idf_component_register(SRCS "hello.c"
                       PRIV_REQUIRES newlib nvs_flash esp_netif esp_event mqtt mosq)
