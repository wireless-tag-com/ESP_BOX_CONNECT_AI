set(UI_DIR ./ui)
file(GLOB_RECURSE UI_SRCS ${UI_DIR}/*.c)

set(APP_DIR ./app)
file(GLOB_RECURSE APP_SRCS ${APP_DIR}/*.c)


idf_component_register(
    SRCS
        "main.c"
        ${UI_SRCS}
        ${APP_SRCS}

    INCLUDE_DIRS
        "."
        ${UI_DIR}
        ${APP_DIR}

)
spiffs_create_partition_image(storage ../spiffs FLASH_IN_PROJECT)

add_definitions(-w)
add_compile_options(-fdiagnostics-color=always)