set(APP_DIR app)
file(GLOB_RECURSE APP_SOURCES_CPP ${APP_DIR}/*.cpp)
file(GLOB_RECURSE APP_SOURCES_C ${APP_DIR}/*.c)

set(APP_SOURCES ${APP_SOURCES_CPP} ${APP_SOURCES_C})

idf_component_register(SRCS "main.cpp" ${APP_SOURCES}
                    INCLUDE_DIRS "." ${APP_DIR}
                    REQUIRES esp_driver_ppa human_face_detect pedestrian_detect coco_detect esp_painter)