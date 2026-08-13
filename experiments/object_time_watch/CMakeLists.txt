cmake_minimum_required(VERSION 3.20)

project(aviutl2_object_time_watch LANGUAGES CXX)

if(NOT WIN32)
    message(FATAL_ERROR "The OBJECT_INFO.time observer targets Windows only.")
endif()

add_executable(object_time_watch object_time_watch.cpp)
target_compile_features(object_time_watch PRIVATE cxx_std_17)
target_compile_options(object_time_watch PRIVATE /EHsc)
target_compile_definitions(object_time_watch PRIVATE
    UNICODE
    _UNICODE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
)
target_link_libraries(object_time_watch PRIVATE dbghelp)
