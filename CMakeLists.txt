cmake_minimum_required(VERSION 3.13)

# Pull in Pico SDK (must be before project)
include($ENV{PICO_SDK_PATH}/external/pico_sdk_import.cmake)

project(pico_ws2812 C CXX ASM)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

# Initialize the Pico SDK
pico_sdk_init()

# Create the WS2812 library
add_library(ws2812
    src/ws2812.cpp
)

target_include_directories(ws2812 PUBLIC 
    ${CMAKE_CURRENT_LIST_DIR}/src
)

pico_generate_pio_header(ws2812 ${CMAKE_CURRENT_LIST_DIR}/src/ws2812.pio)

target_link_libraries(ws2812
    pico_stdlib
    hardware_pio
    hardware_dma
    hardware_clocks
)

# Export targets for other projects to use
install(TARGETS ws2812
    EXPORT ws2812-targets
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib
    RUNTIME DESTINATION bin
    INCLUDES DESTINATION include
)

install(FILES 
    ${CMAKE_CURRENT_LIST_DIR}/src/ws2812.hpp
    DESTINATION include
)

install(EXPORT ws2812-targets
    FILE ws2812-config.cmake
    DESTINATION lib/cmake/ws2812
)

# Only build examples if this is the main project
if(CMAKE_PROJECT_NAME STREQUAL PROJECT_NAME)
    # Add example (optional)
    add_executable(ws2812_example
        examples/example.cpp
    )

    target_link_libraries(ws2812_example
        pico_stdlib
        ws2812
    )

    pico_add_extra_outputs(ws2812_example)
endif()