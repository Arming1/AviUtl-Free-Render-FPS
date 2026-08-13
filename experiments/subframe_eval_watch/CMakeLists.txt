cmake_minimum_required(VERSION 3.20)

project(aviutl2_subframe_eval_watch LANGUAGES CXX)

if(NOT WIN32)
    message(FATAL_ERROR "The AviUtl2 subframe observer targets Windows only.")
endif()

add_executable(subframe_eval_watch subframe_eval_watch.cpp)
target_compile_features(subframe_eval_watch PRIVATE cxx_std_17)
target_compile_definitions(subframe_eval_watch PRIVATE
    UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX
)
