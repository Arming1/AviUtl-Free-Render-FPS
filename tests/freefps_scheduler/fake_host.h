#pragma once

// Fake AviUtl host for the host-free FreeFPS scheduler tests.
//
// Models the host behaviour the scheduler depends on:
//  - the rendered-frame cache is keyed only by the integer frame and keeps the
//    last `cache_depth` distinct keys; a hit returns the cached buffer without
//    running the timeline builder;
//  - a miss runs the builder, which evaluates the active request's
//    target coordinate when the requested integer frame matches.

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

#include <cstddef>
#include <cstdint>

// Counters (reset by fake_reset).
extern int fake_renders;
extern int fake_install_calls;
extern int fake_uninstall_calls;
extern bool fake_installed;

// Resets cache, counters, request state and the install-failure switch.
// Frame size is used when the host renders frames.
void fake_reset(int cache_depth, int width = 64, int height = 36);
void fake_set_install_fails(bool fails);

constexpr DWORD fake_fourcc(char a, char b, char c, char d) {
    return static_cast<DWORD>(static_cast<unsigned char>(a)) |
           (static_cast<DWORD>(static_cast<unsigned char>(b)) << 8) |
           (static_cast<DWORD>(static_cast<unsigned char>(c)) << 16) |
           (static_cast<DWORD>(static_cast<unsigned char>(d)) << 24);
}

// Bytes of one frame for the given format, or 0 for an unknown format.
std::size_t fake_frame_bytes(DWORD format, int width, int height);

// The host-facing video callback (OUTPUT_INFO::func_get_video_ex).
void* fake_get_video(int frame, DWORD format);

// Coordinate stored in the first 8 bytes of a frame buffer.
double fake_read_coordinate(const void* p);

// True if the buffer is exactly what a render at `expected_coordinate` produces.
bool fake_verify_frame(const void* p, std::size_t bytes, double expected_coordinate);
