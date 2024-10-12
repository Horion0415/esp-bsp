set(priv_requires fatfs console)

idf_component_register(SRCS "display_main.c" "app_video.c" "app_usb_msc.c"
                    INCLUDE_DIRS "."
                    PRIV_REQUIRES "${priv_requires}" nvs_flash
                    )

