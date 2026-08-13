#include <windows.h>

#include <algorithm>
#include <array>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cwctype>
#include <string>

#include "output2.h"

namespace {

constexpr int kWarmupSamples = 60;
constexpr int kTargetFrame = 80;
constexpr int kEvictionFrame = 79;

enum class CacheMode {
    baseline,
    resize,
    evict,
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

std::wstring lower_path(LPCWSTR value) {
    std::wstring result = value == nullptr ? L"" : value;
    std::transform(result.begin(), result.end(), result.begin(),
                   [](wchar_t ch) { return static_cast<wchar_t>(std::towlower(ch)); });
    return result;
}

std::uint64_t hash_rgb24(const unsigned char* pixels, int width, int height) {
    constexpr std::uint64_t offset = 14695981039346656037ULL;
    constexpr std::uint64_t prime = 1099511628211ULL;
    std::uint64_t hash = offset;
    if (pixels == nullptr || width <= 0 || height <= 0) {
        return 0;
    }
    const size_t bytes = static_cast<size_t>(width) *
                         static_cast<size_t>(height) * 3;
    for (size_t i = 0; i < bytes; ++i) {
        hash ^= pixels[i];
        hash *= prime;
    }
    return hash;
}

bool request_target(OUTPUT_INFO* output, HANDLE log, int target_index,
                    double coordinate) {
    LARGE_INTEGER begin{};
    LARGE_INTEGER end{};
    QueryPerformanceCounter(&begin);
    write_log(log,
              "event=target_begin target_index=%d requested_frame=%d "
              "expected_coordinate=%.17g qpc=%lld thread_id=%lu\r\n",
              target_index, kTargetFrame, coordinate,
              static_cast<long long>(begin.QuadPart),
              static_cast<unsigned long>(GetCurrentThreadId()));
    const auto* pixels = static_cast<const unsigned char*>(
        output->func_get_video(kTargetFrame, BI_RGB));
    QueryPerformanceCounter(&end);
    if (pixels == nullptr) {
        write_log(log,
                  "event=target_end target_index=%d result=null qpc=%lld\r\n",
                  target_index, static_cast<long long>(end.QuadPart));
        return false;
    }
    write_log(log,
              "event=target_end target_index=%d requested_frame=%d "
              "expected_coordinate=%.17g hash_fnv1a64=%016llx qpc=%lld "
              "thread_id=%lu\r\n",
              target_index, kTargetFrame, coordinate,
              static_cast<unsigned long long>(
                  hash_rgb24(pixels, output->w, output->h)),
              static_cast<long long>(end.QuadPart),
              static_cast<unsigned long>(GetCurrentThreadId()));
    return true;
}

bool request_eviction(OUTPUT_INFO* output, HANDLE log, int after_target) {
    const auto* pixels = static_cast<const unsigned char*>(
        output->func_get_video(kEvictionFrame, BI_RGB));
    const std::uint64_t hash = hash_rgb24(pixels, output->w, output->h);
    write_log(log,
              "event=eviction after_target=%d requested_frame=%d result=%s "
              "hash_fnv1a64=%016llx thread_id=%lu\r\n",
              after_target, kEvictionFrame, pixels == nullptr ? "null" : "ok",
              static_cast<unsigned long long>(hash),
              static_cast<unsigned long>(GetCurrentThreadId()));
    return pixels != nullptr;
}

bool func_output(OUTPUT_INFO* output) {
    if (output == nullptr || output->savefile == nullptr ||
        output->func_get_video == nullptr || output->w <= 0 ||
        output->h <= 0 || output->rate <= 0 || output->scale <= 0 ||
        output->n <= kTargetFrame) {
        return false;
    }
    HANDLE log = CreateFileW(output->savefile, GENERIC_WRITE,
                             FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                             CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (log == INVALID_HANDLE_VALUE) {
        return false;
    }

    const std::wstring path = lower_path(output->savefile);
    const bool sequence_b = path.size() >= 6 &&
                            path.compare(path.size() - 6, 6, L"_b.log") == 0;
    CacheMode mode = CacheMode::baseline;
    if (path.find(L"resize") != std::wstring::npos) {
        mode = CacheMode::resize;
    } else if (path.find(L"evict") != std::wstring::npos) {
        mode = CacheMode::evict;
    }
    const char* mode_name = mode == CacheMode::resize ? "resize" :
                            mode == CacheMode::evict ? "evict" : "baseline";
    constexpr std::array<double, 3> sequence_a{80.0, 80.5, 80.0};
    constexpr std::array<double, 3> sequence_b_values{80.5, 80.0, 80.5};
    const auto& coordinates = sequence_b ? sequence_b_values : sequence_a;

    if (output->func_set_buffer_size != nullptr) {
        output->func_set_buffer_size(1, 0);
    }
    LARGE_INTEGER frequency{};
    QueryPerformanceFrequency(&frequency);
    write_log(log,
              "cache_bypass_probe_version=1 project_rate=%d project_scale=%d "
              "project_frames=%d sequence=%s cache_mode=%s "
              "qpc_frequency=%lld\r\n",
              output->rate, output->scale, output->n,
              sequence_b ? "B" : "A", mode_name,
              static_cast<long long>(frequency.QuadPart));

    bool success = true;
    for (int frame = 0; frame < kWarmupSamples; ++frame) {
        if (output->func_get_video(frame, BI_RGB) == nullptr) {
            success = false;
            break;
        }
    }
    write_log(log, "event=warmup_end requests=%d success=%d\r\n",
              kWarmupSamples, success ? 1 : 0);

    for (int i = 0; success && i < 3; ++i) {
        if (mode == CacheMode::resize && output->func_set_buffer_size != nullptr) {
            const int video_buffers = (i % 2 == 0) ? 1 : 2;
            output->func_set_buffer_size(video_buffers, 0);
            write_log(log,
                      "event=buffer_resize target_index=%d video_buffers=%d\r\n",
                      i, video_buffers);
        }
        success = request_target(output, log, i, coordinates[i]) && success;
        if (success && mode == CacheMode::evict && i < 2) {
            success = request_eviction(output, log, i) && success;
        }
    }

    write_log(log, "event=output_end success=%d\r\n", success ? 1 : 0);
    CloseHandle(log);
    return success;
}

OUTPUT_PLUGIN_TABLE g_plugin{
    OUTPUT_PLUGIN_TABLE::FLAG_VIDEO,
    L"FreeRenderFPS Phase 6 Cache Bypass Probe",
    L"Phase 6 cache probe (*.log)\0*.log\0",
    L"Debug-only cache collision and bypass experiment",
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
