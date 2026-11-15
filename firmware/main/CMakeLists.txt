idf_component_register(
    SRCS "main.cpp" "market.cpp" "weather.cpp"
         "../../physics/physics.cpp" "../../physics/calibration.cpp"
         "../../optimizer/optimizer.cpp" "../../forecast/pricing.cpp"
         "../../forecast/validity.cpp" "../../forecast/sources.cpp"
         "../../forecast/fmi.cpp"
         "../../safety/supervisor.cpp"
         "../../pump/heishamon_adapter.cpp"
    INCLUDE_DIRS "../.."
    REQUIRES esp_eth esp_netif esp_event esp_http_server esp_http_client
             nvs_flash driver json esp_hw_support mbedtls
)
