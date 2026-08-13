cmake_minimum_required(VERSION 3.20)

project(aviutl2_timeline_eval_probe LANGUAGES CXX)

option(PHASE3_DEBUG_BREAK "Break once in the first filter callback for Phase 3 dynamic analysis" OFF)
option(PHASE3_PROBE_IDENTITY "Use the unique Phase 3 effect name and settings key" OFF)

if(NOT WIN32)
    message(FATAL_ERROR "The AviUtl2 timeline evaluation probe targets Windows only.")
endif()

add_library(timeline_eval_probe SHARED timeline_eval_probe.cpp)
target_compile_features(timeline_eval_probe PRIVATE cxx_std_17)
target_include_directories(timeline_eval_probe PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/../../reference/aviutl2_sdk"
)
target_compile_definitions(timeline_eval_probe PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
if(PHASE3_DEBUG_BREAK)
    target_compile_definitions(timeline_eval_probe PRIVATE PHASE3_DEBUG_BREAK=1)
endif()
if(PHASE3_PROBE_IDENTITY)
    target_compile_definitions(timeline_eval_probe PRIVATE PHASE3_PROBE_IDENTITY=1)
endif()

set_target_properties(timeline_eval_probe PROPERTIES
    PREFIX ""
    SUFFIX ".auf2"
)
