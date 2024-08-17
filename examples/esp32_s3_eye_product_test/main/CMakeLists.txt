set(APP_DIR app)
file(GLOB_RECURSE APP_SOURCES ${APP_DIR}/*.c)

idf_component_register(
    SRCS "main.c" ${APP_SOURCES}
    INCLUDE_DIRS "." ${APP_DIR})
