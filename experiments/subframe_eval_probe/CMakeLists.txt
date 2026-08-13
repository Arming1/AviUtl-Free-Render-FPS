cmake_minimum_required(VERSION 3.20)

project(aviutl2_subframe_eval_probe LANGUAGES CXX)

if(NOT WIN32)
    message(FATAL_ERROR "The AviUtl2 subframe evaluation probe targets Windows only.")
endif()

add_library(subframe_eval_probe SHARED subframe_eval_probe.cpp)
target_compile_features(subframe_eval_probe PRIVATE cxx_std_17)
target_include_directories(subframe_eval_probe PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/../../reference/aviutl2_sdk"
)
target_compile_definitions(subframe_eval_probe PRIVATE
    UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX
)

set_target_properties(subframe_eval_probe PROPERTIES
    PREFIX ""
    SUFFIX ".auo2"
)
