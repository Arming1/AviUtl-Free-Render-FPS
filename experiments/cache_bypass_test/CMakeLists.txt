cmake_minimum_required(VERSION 3.20)

project(aviutl2_cache_bypass_test LANGUAGES CXX)

if(NOT WIN32)
    message(FATAL_ERROR "The AviUtl2 Phase 6 probes target Windows only.")
endif()

add_library(cache_bypass_probe SHARED cache_bypass_probe.cpp)
target_compile_features(cache_bypass_probe PRIVATE cxx_std_17)
target_include_directories(cache_bypass_probe PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/../../reference/aviutl2_sdk"
)
target_compile_definitions(cache_bypass_probe PRIVATE
    UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX
)
set_target_properties(cache_bypass_probe PROPERTIES
    PREFIX ""
    SUFFIX ".auo2"
)

add_executable(cache_bypass_watch
    ../subframe_scheduler_test/subframe_scheduler_watch.cpp
)
target_compile_features(cache_bypass_watch PRIVATE cxx_std_17)
target_compile_definitions(cache_bypass_watch PRIVATE
    UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX
)
