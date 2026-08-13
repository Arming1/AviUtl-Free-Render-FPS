#include <windows.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdarg>
#include <cstdio>
#include <cstdint>

#include "output2.h"

namespace {

std::atomic_uint64_t g_output_callback_order{0};

std::uint64_t sample_hash(const void* data, size_t size) {
    if (data == nullptr || size == 0) {
        return 0;
    }

    constexpr std::uint64_t fnv_offset = 14695981039346656037ULL;
    constexpr std::uint64_t fnv_prime = 1099511628211ULL;
    std::uint64_t hash = fnv_offset;
    const auto* bytes = static_cast<const unsigned char*>(data);
    for (size_t i = 0; i < size; ++i) {
        hash ^= bytes[i];
        hash *= fnv_prime;
    }
    return hash;
}

void write_log(HANDLE file, const char* format, ...) {
    std::array<char, 1024> buffer{};

    va_list args;
    va_start(args, format);
    const int length = vsnprintf(buffer.data(), buffer.size(), format, args);
    va_end(args);

    if (length <= 0) {
        return;
    }

    const DWORD bytes_to_write = static_cast<DWORD>(
        (std::min)(static_cast<size_t>(length), buffer.size() - 1));
    DWORD bytes_written = 0;
    WriteFile(file, buffer.data(), bytes_to_write, &bytes_written, nullptr);
}

bool func_output(OUTPUT_INFO* oip) {
    if (oip == nullptr || oip->savefile == nullptr) {
        return false;
    }

    HANDLE log = CreateFileW(
        oip->savefile,
        GENERIC_WRITE,
        FILE_SHARE_READ,
        nullptr,
        CREATE_ALWAYS,
        FILE_ATTRIBUTE_NORMAL,
        nullptr);
    if (log == INVALID_HANDLE_VALUE) {
        return false;
    }

    const std::uint64_t callback_order = ++g_output_callback_order;
    std::uint64_t event_order = 0;

    LARGE_INTEGER performance_frequency{};
    QueryPerformanceFrequency(&performance_frequency);

    write_log(log, "render_probe_version=3\r\n");
    write_log(log,
              "qpc_frequency=%lld\r\n",
              static_cast<long long>(performance_frequency.QuadPart));
    LARGE_INTEGER callback_begin_counter{};
    QueryPerformanceCounter(&callback_begin_counter);
    write_log(log,
              "event=%llu type=output_callback_begin callback_order=%llu qpc=%lld thread_id=%lu\r\n",
              static_cast<unsigned long long>(event_order++),
              static_cast<unsigned long long>(callback_order),
              static_cast<long long>(callback_begin_counter.QuadPart),
              static_cast<unsigned long>(GetCurrentThreadId()));
    write_log(log, "project_fps_rate=%d\r\n", oip->rate);
    write_log(log, "project_fps_scale=%d\r\n", oip->scale);
    write_log(log, "project_fps=%.12f\r\n",
              oip->scale != 0 ? static_cast<double>(oip->rate) / oip->scale : 0.0);
    write_log(log, "total_output_frames=%d\r\n", oip->n);
    write_log(log,
              "note=OUTPUT_INFO.n is the host-selected output range, not a documented project-length formula\r\n");

    if (oip->rate <= 0 || oip->scale <= 0 || oip->n < 0) {
        write_log(log,
                  "event=%llu type=invalid_output_info rate=%d scale=%d frames=%d\r\n",
                  static_cast<unsigned long long>(event_order++),
                  oip->rate,
                  oip->scale,
                  oip->n);
        CloseHandle(log);
        return false;
    }

    if ((oip->flag & OUTPUT_INFO::FLAG_VIDEO) == 0 || oip->func_get_video == nullptr) {
        write_log(log,
                  "event=%llu type=no_video_callback\r\n",
                  static_cast<unsigned long long>(event_order++));
        write_log(log,
                  "event=%llu type=output_callback_end callback_order=%llu\r\n",
                  static_cast<unsigned long long>(event_order++),
                  static_cast<unsigned long long>(callback_order));
        CloseHandle(log);
        return true;
    }

    if (oip->func_set_buffer_size != nullptr) {
        oip->func_set_buffer_size(1, 0);
    }

    // A small, valid, non-monotonic request set. This tests whether
    // the public callback honors arbitrary integer frame indices without creating
    // an encoded video or pretending that repeated frames are higher-FPS samples.
    std::array<int, 4> candidates{0, oip->n - 1, oip->n / 2, 1};
    std::array<int, 4> requested{};
    size_t requested_count = 0;

    auto request_frame = [&](int frame, long long repeat_of_request) {
        if (oip->func_is_abort != nullptr && oip->func_is_abort()) {
            write_log(log,
                      "event=%llu type=abort_before_request request_order=%zu\r\n",
                      static_cast<unsigned long long>(event_order++),
                      requested_count);
            return false;
        }

        const size_t request_order = requested_count++;
        const double implied_timestamp =
            static_cast<double>(frame) * oip->scale / oip->rate;
        LARGE_INTEGER start_counter{};
        QueryPerformanceCounter(&start_counter);
        write_log(log,
                  "event=%llu type=video_request_begin request_order=%zu frame=%d implied_timestamp_seconds=%.12f repeat_of_request=%lld qpc=%lld thread_id=%lu\r\n",
                  static_cast<unsigned long long>(event_order++),
                  request_order,
                  frame,
                  implied_timestamp,
                  repeat_of_request,
                  static_cast<long long>(start_counter.QuadPart),
                  static_cast<unsigned long>(GetCurrentThreadId()));

        LARGE_INTEGER end_counter{};
        const void* frame_data = oip->func_get_video(frame, BI_RGB);
        QueryPerformanceCounter(&end_counter);

        const size_t image_bytes =
            static_cast<size_t>(oip->w) * static_cast<size_t>(oip->h) * 3;
        const size_t hash_bytes = image_bytes;
        const std::uint64_t hash = sample_hash(frame_data, hash_bytes);
        const double elapsed_ms = performance_frequency.QuadPart > 0
            ? static_cast<double>(end_counter.QuadPart - start_counter.QuadPart) *
                  1000.0 / performance_frequency.QuadPart
            : 0.0;

        write_log(log,
                  "event=%llu type=video_request_end request_order=%zu frame=%d result=%s buffer=%p sample_bytes=%zu sample_hash_fnv1a64=%016llx callback_elapsed_ms=%.6f qpc=%lld thread_id=%lu\r\n",
                  static_cast<unsigned long long>(event_order++),
                  request_order,
                  frame,
                  frame_data != nullptr ? "non_null" : "null",
                  frame_data,
                  frame_data != nullptr ? hash_bytes : 0,
                  static_cast<unsigned long long>(hash),
                  elapsed_ms,
                  static_cast<long long>(end_counter.QuadPart),
                  static_cast<unsigned long>(GetCurrentThreadId()));

        if (oip->func_rest_time_disp != nullptr) {
            oip->func_rest_time_disp(static_cast<int>(requested_count), 5);
        }
        return true;
    };

    for (const int frame : candidates) {
        if (frame < 0 || frame >= oip->n) {
            continue;
        }

        bool duplicate = false;
        for (size_t i = 0; i < requested_count; ++i) {
            if (requested[i] == frame) {
                duplicate = true;
                break;
            }
        }
        if (duplicate) {
            continue;
        }
        requested[requested_count] = frame;
        if (!request_frame(frame, -1)) {
            break;
        }
    }

    if (requested_count > 0) {
        // Diagnostic repeat only. The frame is not written to an encoder or
        // counted as a higher-FPS output sample. Prefer the last-frame request
        // because the runtime fixture contains a visible object there.
        const size_t repeat_index = requested_count > 1 ? 1 : 0;
        request_frame(requested[repeat_index], static_cast<long long>(repeat_index));
    }

    LARGE_INTEGER callback_end_counter{};
    QueryPerformanceCounter(&callback_end_counter);
    write_log(log,
              "event=%llu type=output_callback_end callback_order=%llu requests=%zu qpc=%lld thread_id=%lu\r\n",
              static_cast<unsigned long long>(event_order++),
              static_cast<unsigned long long>(callback_order),
              requested_count,
              static_cast<long long>(callback_end_counter.QuadPart),
              static_cast<unsigned long>(GetCurrentThreadId()));
    CloseHandle(log);
    return true;
}

OUTPUT_PLUGIN_TABLE g_output_plugin{
    OUTPUT_PLUGIN_TABLE::FLAG_VIDEO,
    L"Render Pipeline Probe",
    L"Render probe log (*.log)\0*.log\0",
    L"AviUtl2 integer-frame render pipeline probe (Phase 1)",
    func_output,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
};

} // namespace

extern "C" __declspec(dllexport) bool InitializePlugin(DWORD) {
    return true;
}

extern "C" __declspec(dllexport) void UninitializePlugin() {
}

extern "C" __declspec(dllexport) OUTPUT_PLUGIN_TABLE* GetOutputPluginTable() {
    return &g_output_plugin;
}
