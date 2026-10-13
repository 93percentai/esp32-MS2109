cmake_minimum_required(VERSION 3.16)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(ms2109_wireless_display)
if(NOT IDF_TARGET STREQUAL "esp32s31")
    message(FATAL_ERROR "This firmware requires ESP32-S31 (idf.py --preview set-target esp32s31)")
endif()

# LVGL 9.5's PPA CMake dependency and cache-line check name only account for P4.
# S31 has PPA and a 64-byte L1 data cache; keep the compatibility alias local to LVGL.
idf_component_get_property(lvgl_lib lvgl__lvgl COMPONENT_LIB)
target_link_libraries(${lvgl_lib} PRIVATE idf::esp_driver_ppa idf::esp_mm)
target_compile_definitions(${lvgl_lib} PRIVATE
    CONFIG_CACHE_L2_CACHE_LINE_SIZE=CONFIG_CACHE_L1_DCACHE_LINE_SIZE)
