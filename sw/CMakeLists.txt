# ======================================================
# FreeRTOS
# ======================================================

set(FREERTOS_KERNEL_PATH${CMAKE_CURRENT_SOURCE_DIR}/FreeRTOS-Kernel)

include(${CMAKE_CURRENT_SOURCE_DIR}/FreeRTOS_Kernel_import.cmake)

# ======================================================
# LVGL
# ======================================================

set(CONFIG_LV_BUILD_EXAMPLES ON)
set(CONFIG_LV_BUILD_DEMOS ON)

set(LV_BUILD_CONF_PATH
    ${CMAKE_CURRENT_SOURCE_DIR}/include/lv_conf.h)

add_subdirectory(lvgl)

# ======================================================
# Includes
# ======================================================

include_directories(include)
include_directories(lvgl)
include_directories(lvgl/demos)

# ======================================================
# Executable
# ======================================================

add_executable(cs122_demo

    src/main.cpp

    # LVGL/display
    src/spi_display.cpp
    src/cs122_app.cpp
    src/lvgl_touch.cpp
    src/lvgl_clock.cpp
    src/lvgl_pie_chart.cpp
    src/lvgl_compass.cpp
    src/lvgl_arc_label.cpp
    src/lvgl_heart_rate.cpp
    src/lvgl_demo_widgets.cpp
)

# ======================================================
# Compile Definitions
# ======================================================

add_definitions(-DBUILD_LVGL=1)

# ======================================================
# Libraries
# ======================================================

target_link_libraries(cs122_demo

    # Pico
    pico_stdlib
    hardware_spi
    hardware_adc
    pico_cyw43_arch_none

    # LVGL
    lvgl::lvgl

    # FreeRTOS
    pico_async_context_freertos
    FreeRTOS-Kernel-Heap4
)

# ======================================================
# STDIO
# ======================================================

pico_enable_stdio_uart(cs122_demo 1)
pico_enable_stdio_usb(cs122_demo 1)

# ======================================================
# Outputs
# ======================================================

pico_add_extra_outputs(cs122_demo)