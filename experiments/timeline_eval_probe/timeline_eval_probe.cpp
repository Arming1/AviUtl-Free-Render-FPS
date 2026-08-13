#include <windows.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdarg>
#include <cstdio>
#include <cwchar>

#include "filter2.h"

namespace {

std::atomic_uint64_t g_call_order{0};
#if defined(PHASE3_DEBUG_BREAK)
std::atomic_bool g_debug_break_once{false};
#endif
SRWLOCK g_log_lock = SRWLOCK_INIT;
std::array<wchar_t, MAX_PATH> g_log_path{};

bool func_proc_video(FILTER_PROC_VIDEO* video);

#if defined(PHASE3_PROBE_IDENTITY)
auto g_probe_marker = FILTER_ITEM_CHECK(L"FRFPS_P3_B91D6E20_marker", false);
#else
auto g_probe_marker = FILTER_ITEM_CHECK(L"FRFPS_P2_7F3A9C42_marker", false);
#endif
void* g_items[] = {&g_probe_marker, nullptr};

FILTER_PLUGIN_TABLE g_filter_plugin{
    FILTER_PLUGIN_TABLE::FLAG_VIDEO,
#if defined(PHASE3_PROBE_IDENTITY)
    L"FRFPS Object Time Writer Probe B91D6E20",
    L"FRFPS Object Time Writer Probe B91D6E20",
    L"Phase 3 debug-only OBJECT_INFO writer probe",
#else
    L"FRFPS Timeline Eval Probe 7F3A9C42",
    L"FRFPS Timeline Eval Probe 7F3A9C42",
    L"Phase 2 no-op timing probe; logs OBJECT_INFO frame/time",
#endif
    g_items,
    func_proc_video,
    nullptr,
};

void initialize_log_path() {
    HMODULE module = nullptr;
    GetModuleHandleExW(
        GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
            GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
        reinterpret_cast<LPCWSTR>(&initialize_log_path),
        &module);

    const DWORD length = GetModuleFileNameW(
        module, g_log_path.data(), static_cast<DWORD>(g_log_path.size()));
    if (length == 0 || length >= g_log_path.size()) {
        g_log_path[0] = L'\0';
        return;
    }

    wchar_t* separator = wcsrchr(g_log_path.data(), L'\\');
    if (separator == nullptr) {
        g_log_path[0] = L'\0';
        return;
    }
    *(separator + 1) = L'\0';
    wcscat_s(g_log_path.data(), g_log_path.size(), L"timeline_eval_probe.log");
}

void append_log(const char* format, ...) {
    if (g_log_path[0] == L'\0') {
        return;
    }

    std::array<char, 1024> buffer{};
    va_list args;
    va_start(args, format);
    const int length = vsnprintf(buffer.data(), buffer.size(), format, args);
    va_end(args);
    if (length <= 0) {
        return;
    }

    AcquireSRWLockExclusive(&g_log_lock);
    HANDLE file = CreateFileW(
        g_log_path.data(),
        FILE_APPEND_DATA,
        FILE_SHARE_READ | FILE_SHARE_WRITE,
        nullptr,
        OPEN_ALWAYS,
        FILE_ATTRIBUTE_NORMAL,
        nullptr);
    if (file != INVALID_HANDLE_VALUE) {
        const DWORD bytes = static_cast<DWORD>((std::min)(
            static_cast<size_t>(length), buffer.size() - 1));
        DWORD written = 0;
        WriteFile(file, buffer.data(), bytes, &written, nullptr);
        CloseHandle(file);
    }
    ReleaseSRWLockExclusive(&g_log_lock);
}

bool func_proc_video(FILTER_PROC_VIDEO* video) {
    if (video == nullptr || video->scene == nullptr || video->object == nullptr) {
        return true;
    }

    LARGE_INTEGER qpc{};
    QueryPerformanceCounter(&qpc);
    const auto* scene = video->scene;
    const auto* object = video->object;
    append_log(
        "call=%llu qpc=%lld thread_id=%lu scene_rate=%d scene_scale=%d "
        "proc=%p scene=%p object=%p frame_addr=%p time_addr=%p "
        "object_id=%lld effect_id=%lld frame=%d frame_total=%d "
        "time_seconds=%.17g time_total_seconds=%.17g layer=%d "
        "frame_s=%d frame_e=%d sample_index=%lld sample_num=%d\r\n",
        static_cast<unsigned long long>(++g_call_order),
        static_cast<long long>(qpc.QuadPart),
        static_cast<unsigned long>(GetCurrentThreadId()),
        scene->rate,
        scene->scale,
        video,
        scene,
        object,
        &object->frame,
        &object->time,
        static_cast<long long>(object->id),
        static_cast<long long>(object->effect_id),
        object->frame,
        object->frame_total,
        object->time,
        object->time_total,
        object->layer,
        object->frame_s,
        object->frame_e,
        static_cast<long long>(object->sample_index),
        object->sample_num);
#if defined(PHASE3_DEBUG_BREAK)
    if (!g_debug_break_once.exchange(true)) {
        append_log("debug_break=before_first_filter_return object=%p time_addr=%p\r\n",
                   object,
                   &object->time);
        __debugbreak();
    }
#endif
    return true;
}

} // namespace

extern "C" __declspec(dllexport) bool InitializePlugin(DWORD) {
    initialize_log_path();
    if (g_log_path[0] != L'\0') {
        DeleteFileW(g_log_path.data());
    }
    LARGE_INTEGER frequency{};
    QueryPerformanceFrequency(&frequency);
    append_log(
        "timeline_eval_probe_version=1 qpc_frequency=%lld note=no_image_mutation\r\n",
        static_cast<long long>(frequency.QuadPart));
    return true;
}

extern "C" __declspec(dllexport) void UninitializePlugin() {
}

extern "C" __declspec(dllexport) FILTER_PLUGIN_TABLE* GetFilterPluginTable() {
    return &g_filter_plugin;
}
