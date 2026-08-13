cmake_minimum_required(VERSION 3.20)

project(aviutl2_render_probe LANGUAGES CXX)

if(NOT WIN32)
    message(FATAL_ERROR "The AviUtl2 render probe targets Windows only.")
endif()

add_library(render_probe SHARED render_probe.cpp)
target_compile_features(render_probe PRIVATE cxx_std_17)
target_include_directories(render_probe PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/../../reference/aviutl2_sdk"
)
target_compile_definitions(render_probe PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)

set_target_properties(render_probe PROPERTIES
    PREFIX ""
    SUFFIX ".auo2"
)
