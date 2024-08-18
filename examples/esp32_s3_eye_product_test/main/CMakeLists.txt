set(APP_DIR app)
file(GLOB_RECURSE APP_SOURCES ${APP_DIR}/*.c)
set(LV_UI_DIR ui)
file(GLOB_RECURSE LV_UI_SOURCES ${LV_UI_DIR}/*.c)

idf_component_register(
    SRCS "main.c" ${APP_SOURCES} ${LV_UI_SOURCES}
    INCLUDE_DIRS "." ${APP_DIR} ${LV_UI_DIR})
