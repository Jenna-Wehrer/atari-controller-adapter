cmake_minimum_required(VERSION 3.13)

include(pico_sdk_import.cmake)

project(atari_adapter)
set(CMAKE_C_STANDARD 11)

pico_sdk_init()

add_executable(atari_adapter
    main.c
    usb_descriptors.c
)

target_include_directories(atari_adapter PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(atari_adapter PRIVATE
    pico_stdlib
    tinyusb_device
    hardware_structs
)

target_compile_definitions(atari_adapter PRIVATE
    TUSB_CONFIG_FILE="tusb_config.h"
)

pico_enable_stdio_usb(atari_adapter 0)
pico_enable_stdio_uart(atari_adapter 0)

pico_add_extra_outputs(atari_adapter)