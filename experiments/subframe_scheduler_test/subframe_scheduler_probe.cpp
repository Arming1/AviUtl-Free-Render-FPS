#include <windows.h>

#include <algorithm>
#include <array>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <limits>

#include "output2.h"

namespace {

constexpr int kOutputSamples = 60;
constexpr int kCacheFrame = 80;
constexpr int kCacheRequests = 3;

struct PixelStats {
    std::uint64_t hash = 14695981039346656037ULL;
    std::uint64_t non_black_pixels = 0;
    std::uint64_t sum_x = 0;
    std::uint64_t sum_y = 0;
    int min_x = (std::numeric_limits<int>::max)();
    int min_y = (std::numeric_limits<int>::max)();
    int max_x = -1;
    int max_y = -1;
};

void write_log(HANDLE file, const char* format, ...) {
    std::array<char, 1536> buffer{};
    va_list args;
    va_start(args, format);
    const int length = vsnprintf(buffer.data(), buffer.size(), format, args);
    va_end(args);
    if (length <= 0) {
        return;
    }
    const DWORD bytes = static_cast<DWORD>((std::min)(
        static_cast<size_t>(length), buffer.size() - 1));
    DWORD written = 0;
    WriteFile(file, buffer.data(), bytes, &written, nullptr);
}

PixelStats analyze_rgb24(const unsigned char* pixels, int width, int height) {
    PixelStats result;
    if (pixels == nullptr || width <= 0 || height <= 0) {
        return result;
    }
    constexpr std::uint64_t fnv_prime = 1099511628211ULL;
    const size_t byte_count = static_cast<size_t>(width) *
                              static_cast<size_t>(height) * 3;
    for (size_t i = 0; i < byte_count; ++i) {
        result.hash ^= pixels[i];
        result.hash *= fnv_prime;
    }
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const size_t offset = (static_cast<size_t>(y) * width + x) * 3;
            if (pixels[offset] == 0 && pixels[offset + 1] == 0 &&
                pixels[offset + 2] == 0) {
                continue;
            }
            ++result.non_black_pixels;
            result.sum_x += static_cast<std::uint64_t>(x);
            result.sum_y += static_cast<std::uint64_t>(y);
            result.min_x = (std::min)(result.min_x, x);
            result.min_y = (std::min)(result.min_y, y);
            result.max_x = (std::max)(result.max_x, x);
            result.max_y = (std::max)(result.max_y, y);
        }
    }
    return result;
}

bool request_and_log(OUTPUT_INFO* output, HANDLE log, int ordinal,
                     const char* kind, int requested_frame,
                     double expected_coordinate) {
    LARGE_INTEGER begin{};
    LARGE_INTEGER end{};
    QueryPerformanceCounter(&begin);
    write_log(log,
              "event=request_begin ordinal=%d kind=%s requested_frame=%d "
              "expected_coordinate=%.17g qpc=%lld thread_id=%lu\r\n",
              ordinal, kind, requested_frame, expected_coordinate,
              static_cast<long long>(begin.QuadPart),
              static_cast<unsigned long>(GetCurrentThreadId()));

    const auto* pixels = static_cast<const unsigned char*>(
        output->func_get_video(requested_frame, BI_RGB));
    QueryPerformanceCounter(&end);
    if (pixels == nullptr) {
        write_log(log,
                  "event=request_end ordinal=%d kind=%s requested_frame=%d "
                  "result=null qpc=%lld thread_id=%lu\r\n",
                  ordinal, kind, requested_frame,
                  static_cast<long long>(end.QuadPart),
                  static_cast<unsigned long>(GetCurrentThreadId()));
        return false;
    }

    const PixelStats stats = analyze_rgb24(pixels, output->w, output->h);
    const double centroid_x = stats.non_black_pixels == 0
        ? 0.0
        : static_cast<double>(stats.sum_x) / stats.non_black_pixels;
    const double centroid_y = stats.non_black_pixels == 0
        ? 0.0
        : static_cast<double>(stats.sum_y) / stats.non_black_pixels;
    write_log(log,
              "event=request_end ordinal=%d kind=%s requested_frame=%d "
              "expected_coordinate=%.17g result=ok hash_fnv1a64=%016llx "
              "non_black_pixels=%llu bbox=%d,%d,%d,%d centroid=%.17g,%.17g "
              "qpc=%lld thread_id=%lu\r\n",
              ordinal, kind, requested_frame, expected_coordinate,
              static_cast<unsigned long long>(stats.hash),
              static_cast<unsigned long long>(stats.non_black_pixels),
              stats.min_x, stats.min_y, stats.max_x, stats.max_y,
              centroid_x, centroid_y, static_cast<long long>(end.QuadPart),
              static_cast<unsigned long>(GetCurrentThreadId()));
    return true;
}

bool func_output(OUTPUT_INFO* output) {
    if (output == nullptr || output->savefile == nullptr ||
        output->func_get_video == nullptr || output->rate <= 0 ||
        output->scale <= 0 || output->w <= 0 || output->h <= 0 ||
        output->n <= kCacheFrame) {
        return false;
    }
    HANDLE log = CreateFileW(output->savefile, GENERIC_WRITE,
                             FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                             CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (log == INVALID_HANDLE_VALUE) {
        return false;
    }
    if (output->func_set_buffer_size != nullptr) {
        output->func_set_buffer_size(1, 0);
    }

    LARGE_INTEGER frequency{};
    QueryPerformanceFrequency(&frequency);
    write_log(log,
              "subframe_scheduler_probe_version=1 qpc_frequency=%lld "
              "project_rate=%d project_scale=%d project_frames=%d "
              "output_samples=%d mapping=sample_index*0.5\r\n",
              static_cast<long long>(frequency.QuadPart), output->rate,
              output->scale, output->n, kOutputSamples);

    bool success = true;
    int ordinal = 0;
    for (int sample = 0; sample < kOutputSamples; ++sample, ++ordinal) {
        if (output->func_is_abort != nullptr && output->func_is_abort()) {
            success = false;
            break;
        }
        success = request_and_log(output, log, ordinal, "sample", sample,
                                  static_cast<double>(sample) * 0.5) && success;
        if (output->func_rest_time_disp != nullptr) {
            output->func_rest_time_disp(sample + 1, kOutputSamples);
        }
    }

    constexpr std::array<double, kCacheRequests> cache_coordinates{
        80.0, 80.5, 80.0
    };
    for (double coordinate : cache_coordinates) {
        success = request_and_log(output, log, ordinal++, "cache", kCacheFrame,
                                  coordinate) && success;
    }
    write_log(log, "event=output_end requests=%d success=%d\r\n", ordinal,
              success ? 1 : 0);
    CloseHandle(log);
    return success;
}

OUTPUT_PLUGIN_TABLE g_plugin{
    OUTPUT_PLUGIN_TABLE::FLAG_VIDEO,
    L"FreeRenderFPS Phase 5 Scheduler Probe",
    L"Phase 5 scheduler log (*.log)\0*.log\0",
    L"Debug-only 60-sample subframe scheduling and cache probe",
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
    return &g_plugin;
}

