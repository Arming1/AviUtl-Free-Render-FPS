cmake_minimum_required(VERSION 3.20)

project(aviutl2_subframe_scheduler_test LANGUAGES CXX)

if(NOT WIN32)
    message(FATAL_ERROR "The AviUtl2 Phase 5 probes target Windows only.")
endif()

add_library(subframe_scheduler_probe SHARED subframe_scheduler_probe.cpp)
target_compile_features(subframe_scheduler_probe PRIVATE cxx_std_17)
target_include_directories(subframe_scheduler_probe PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/../../reference/aviutl2_sdk"
)
target_compile_definitions(subframe_scheduler_probe PRIVATE
    UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX
)
set_target_properties(subframe_scheduler_probe PROPERTIES
    PREFIX ""
    SUFFIX ".auo2"
)

add_executable(subframe_scheduler_watch subframe_scheduler_watch.cpp)
target_compile_features(subframe_scheduler_watch PRIVATE cxx_std_17)
target_compile_definitions(subframe_scheduler_watch PRIVATE
    UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX
)

