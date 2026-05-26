cmake_minimum_required(VERSION 3.13)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# == DO NOT EDIT THE FOLLOWING LINES for the Raspberry Pi Pico VS Code Extension to work ==
if(WIN32)
    set(USERHOME $ENV{USERPROFILE})
else()
    set(USERHOME $ENV{HOME})
endif()

set(sdkVersion 2.2.0)
set(toolchainVersion 14_2_Rel1)
set(picotoolVersion 2.2.0-a4)

set(picoVscode ${USERHOME}/.pico-sdk/cmake/pico-vscode.cmake)

if (EXISTS ${picoVscode})
    include(${picoVscode})
endif()
# ====================================================================================

set(PICO_BOARD pico2_w CACHE STRING "Board type")

include(pico_sdk_import.cmake)

project(cs122_demo C CXX ASM)

pico_sdk_init()

# =========================
# LVGL CONFIGURATION
# =========================

set(CONFIG_LV_BUILD_EXAMPLES OFF)
set(CONFIG_LV_BUILD_DEMOS OFF)

add_subdirectory(lvgl)

# =========================
# INCLUDE DIRECTORIES
# =========================

include_directories(include)
include_directories(lvgl)

# =========================
# EXECUTABLE
# =========================

add_definitions(-DBUILD_LVGL=1)

add_executable(cs122_demo
    src/main.cpp
    src/spi_display.cpp
    src/cs122_app.cpp
    src/lvgl_clock.cpp
    src/lvgl_pie_chart.cpp
    src/lvgl_compass.cpp
    src/lvgl_arc_label.cpp
    src/lvgl_heart_rate.cpp
)

# =========================
# LIBRARIES
# =========================

target_link_libraries(cs122_demo
    pico_stdlib
    hardware_spi
    pico_cyw43_arch_none
    lvgl
)

# =========================
# STDIO
# =========================

pico_enable_stdio_uart(cs122_demo 0)
pico_enable_stdio_usb(cs122_demo 1)

# =========================
# EXTRA OUTPUTS
# =========================

pico_add_extra_outputs(cs122_demo)