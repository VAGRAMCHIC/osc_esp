idf_component_register(SRCS 
    main.c 
    wifi.c 
    ads1115.c 
    tcp_server.c
    INCLUDE_DIRS "."
    REQUIRES driver nvs_flash esp_wifi esp_event lwip esp_timer
)

#idf_component_register(SRCS "osc_esp.c"
#                    INCLUDE_DIRS ".")
