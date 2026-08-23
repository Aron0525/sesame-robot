idf_component_register(
    SRCS "transport_policy.cpp" "device_config.cpp" "gateway_client.cpp"
    INCLUDE_DIRS "include"
    REQUIRES esp_event esp_netif esp_wifi nvs_flash mdns esp_websocket_client
)
