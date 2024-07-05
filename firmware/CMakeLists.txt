cmake_minimum_required(VERSION 3.13)

# initialize pico-sdk from submodule
# note: this must happen before project()
include(pico_sdk_import.cmake)

project(keyboard_fw)

# initialize the Raspberry Pi Pico SDK
pico_sdk_init()

# rest of your project
add_executable(keyboard_fw
    main.c
    usb_descriptors.c
)

# Make sure TinyUSB can find tusb_config.h
target_include_directories(keyboard_fw PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR})

# Add pico_stdlib library which aggregates commonly used features
target_link_libraries(keyboard_fw PUBLIC pico_stdlib pico_unique_id tinyusb_device tinyusb_board)

# pico_enable_stdio_usb(keyboard_fw 1)
# pico_enable_stdio_uart(keyboard_fw 0)

# create map/bin/hex/uf2 file in addition to ELF.
pico_add_extra_outputs(keyboard_fw)
