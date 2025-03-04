# set(priv_requires fatfs console wear_levelling esp_partition)

# set(APP_DIR app)
# file(GLOB_RECURSE APP_SOURCES ${APP_DIR}/*.c)

# idf_component_register(SRCS "main.c" ${APP_SOURCES}
#                     INCLUDE_DIRS "." ${APP_DIR}
#                     REQUIRES ${priv_requires} nvs_flash esp_wifi esp_driver_ppa)

set(LV_UI_DIR ui)
file(GLOB_RECURSE LV_UI_SOURCES ${LV_UI_DIR}/*.c)

idf_component_register(SRCS "main.c" ${LV_UI_SOURCES}
                    INCLUDE_DIRS "." ${LV_UI_DIR})