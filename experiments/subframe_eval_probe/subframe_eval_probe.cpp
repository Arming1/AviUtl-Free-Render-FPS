#include <windows.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdarg>
#include <cstdio>
#include <limits>

#include "output2.h"

namespace {

constexpr int kTargetFrame = 80;

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
    std::array<char, 1024> buffer{};
    va_list args;
    va_start(args, format);
    const int length = vsnprintf(buffer.data(), buffer.size(), format, args);
    va_end(args);
    if (length <= 0) {
        return;
    }
    const DWORD size = static_cast<DWORD>((std::min)(
        static_cast<size_t>(length), buffer.size() - 1));
    DWORD written = 0;
    WriteFile(file, buffer.data(), size, &written, nullptr);
}

PixelStats analyze_rgb24(const unsigned char* pixels, int width, int height) {
    PixelStats result;
    if (pixels == nullptr || width <= 0 || height <= 0) {
        return result;
    }

    constexpr std::uint64_t fnv_prime = 1099511628211ULL;
    const size_t byte_count = static_cast<size_t>(width) *
                              static_cast<size_t>(height) * 3;
    for (size_t index = 0; index < byte_count; ++index) {
        result.hash ^= pixels[index];
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

bool func_output(OUTPUT_INFO* output) {
    if (output == nullptr || output->savefile == nullptr) {
        return false;
    }
    HANDLE log = CreateFileW(output->savefile, GENERIC_WRITE,
                             FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                             CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (log == INVALID_HANDLE_VALUE) {
        return false;
    }

    LARGE_INTEGER frequency{};
    LARGE_INTEGER begin{};
    QueryPerformanceFrequency(&frequency);
    QueryPerformanceCounter(&begin);
    write_log(log,
              "subframe_eval_probe_version=1 target_frame=%d qpc_frequency=%lld "
              "qpc_begin=%lld thread_id=%lu\r\n",
              kTargetFrame, static_cast<long long>(frequency.QuadPart),
              static_cast<long long>(begin.QuadPart),
              static_cast<unsigned long>(GetCurrentThreadId()));
    write_log(log, "width=%d height=%d rate=%d scale=%d frames=%d\r\n",
              output->w, output->h, output->rate, output->scale, output->n);

    if ((output->flag & OUTPUT_INFO::FLAG_VIDEO) == 0 ||
        output->func_get_video == nullptr || output->n <= kTargetFrame ||
        output->w <= 0 || output->h <= 0) {
        write_log(log, "result=invalid_output_or_target_out_of_range\r\n");
        CloseHandle(log);
        return false;
    }
    if (output->func_set_buffer_size != nullptr) {
        output->func_set_buffer_size(1, 0);
    }

    bool success = true;
    for (int request_index = 0; request_index < 2; ++request_index) {
        LARGE_INTEGER request_begin{};
        LARGE_INTEGER request_end{};
        QueryPerformanceCounter(&request_begin);
        const auto* pixels = static_cast<const unsigned char*>(
            output->func_get_video(kTargetFrame, BI_RGB));
        QueryPerformanceCounter(&request_end);

        if (pixels == nullptr) {
            write_log(log,
                      "result=null_frame request_index=%d "
                      "qpc_request_begin=%lld qpc_request_end=%lld\r\n",
                      request_index,
                      static_cast<long long>(request_begin.QuadPart),
                      static_cast<long long>(request_end.QuadPart));
            success = false;
            break;
        }

        const PixelStats stats = analyze_rgb24(pixels, output->w, output->h);
        const double center_x = stats.non_black_pixels == 0
            ? 0.0
            : static_cast<double>(stats.sum_x) / stats.non_black_pixels;
        const double center_y = stats.non_black_pixels == 0
            ? 0.0
            : static_cast<double>(stats.sum_y) / stats.non_black_pixels;
        write_log(log,
                  "result=ok request_index=%d frame=%d implied_time=%.17g "
                  "hash_fnv1a64=%016llx non_black_pixels=%llu "
                  "bbox=%d,%d,%d,%d centroid=%.17g,%.17g "
                  "qpc_request_begin=%lld qpc_request_end=%lld thread_id=%lu\r\n",
                  request_index, kTargetFrame,
                  static_cast<double>(kTargetFrame) * output->scale /
                      output->rate,
                  static_cast<unsigned long long>(stats.hash),
                  static_cast<unsigned long long>(stats.non_black_pixels),
                  stats.min_x, stats.min_y, stats.max_x, stats.max_y,
                  center_x, center_y,
                  static_cast<long long>(request_begin.QuadPart),
                  static_cast<long long>(request_end.QuadPart),
                  static_cast<unsigned long>(GetCurrentThreadId()));
    }
    CloseHandle(log);
    return success;
}

OUTPUT_PLUGIN_TABLE g_plugin{
    OUTPUT_PLUGIN_TABLE::FLAG_VIDEO,
    L"Subframe Evaluation Probe P4",
    L"Subframe evaluation log (*.log)\0*.log\0",
    L"Phase 4 one-frame hash and bounding-box probe; no image mutation",
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
