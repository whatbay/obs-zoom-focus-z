cmake_minimum_required(VERSION 3.28)
project(obs-zoom-focus VERSION 1.1.0 LANGUAGES C)

find_package(libobs CONFIG REQUIRED)

add_library(obs-zoom-focus MODULE
    src/obs-zoom-focus.c
)

target_link_libraries(obs-zoom-focus PRIVATE OBS::libobs)

set_target_properties(obs-zoom-focus PROPERTIES
    PREFIX ""
)

install(TARGETS obs-zoom-focus
    LIBRARY DESTINATION bin/64bit
    RUNTIME DESTINATION bin/64bit
)

install(DIRECTORY data/
    DESTINATION data
)
