idf_component_register(SRCS "main_comment.c" "pcnt.c"
                    REQUIRES esp_driver_pcnt esp_driver_gpio
                    INCLUDE_DIRS ".")
