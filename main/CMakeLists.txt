idf_component_register(SRCS "osc_esp.c"
                    INCLUDE_DIRS "."
                    REQUIRES nvs_flash esp_timer esp_wifi lwip esp_adc)

#idf_component_register(SRCS "osc_esp.c"
#                    INCLUDE_DIRS ".")
