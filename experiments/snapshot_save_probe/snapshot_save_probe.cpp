// SnapshotSaveProbe: test-only probe, not a production plugin.
// Question: after EDIT_HANDLE::save_project_file(path) saves the current project to another file, (a) does the
// current project path (PROJECT_FILE::get_project_file_path, EDIT_SECTION::get_project_file) change, and (b) does the
// project save handler fire?
//
// Registers:
//   - project load and save handlers: record get_project_file_path() and a call count each time.
//   - Edit > Snapshot Save Probe: reads the project path, clears the save counter, calls save_project_file() to
//     %TEMP%\SnapshotSaveProbe\snapshot.aup2, reads the path again, then shows a MessageBox and appends the same text
//     (UTF-8, with a timestamp) to %TEMP%\SnapshotSaveProbe\result.txt.
//
// Context rules (experiments/free_fps_export/sdk/plugin2.h):
//   - get_project_file is an EDIT_SECTION member: "call_read_section利用不可", so it runs inside call_edit_section_param.
//   - save_project_file: "参照ロック、編集ロック状態では利用出来ません", so it runs outside every section.
//   - The menu is registered with register_edit_menu_param, which calls back without an edit section.

#include <windows.h>

#include <cstdio>
#include <mutex>
#include <string>

#include "plugin2.h"

#pragma comment(lib, "user32.lib")  // MessageBoxW

namespace {

constexpr wchar_t kTitle[] = L"SnapshotSaveProbe";
constexpr wchar_t kMenuName[] = L"Snapshot Save Probe";
constexpr wchar_t kSnapshotDir[] = L"%TEMP%\\SnapshotSaveProbe";
constexpr wchar_t kSnapshotPath[] = L"%TEMP%\\SnapshotSaveProbe\\snapshot.aup2";
constexpr wchar_t kResultName[] = L"result.txt";

EDIT_HANDLE* g_edit = nullptr;

std::mutex g_state_mutex;
int g_load_count = 0;
std::wstring g_last_load_path = L"(尚未回调)";
int g_save_count = 0;
std::wstring g_last_save_path = L"(尚未回调)";

// Path text for display. nullptr and empty are shown differently.
std::wstring describe_path(LPCWSTR path) {
    if (path == nullptr) {
        return L"<nullptr>";
    }
    if (*path == L'\0') {
        return L"<空>";
    }
    return path;
}

std::wstring project_path_of(PROJECT_FILE* project) {
    if (project == nullptr || project->get_project_file_path == nullptr) {
        return L"<PROJECT_FILE 为空>";
    }
    return describe_path(project->get_project_file_path());
}

void on_project_load(PROJECT_FILE* project) {
    const std::wstring path = project_path_of(project);
    std::lock_guard<std::mutex> lock(g_state_mutex);
    ++g_load_count;
    g_last_load_path = path;
}

void on_project_save(PROJECT_FILE* project) {
    const std::wstring path = project_path_of(project);
    std::lock_guard<std::mutex> lock(g_state_mutex);
    ++g_save_count;
    g_last_save_path = path;
}

struct PathReading {
    bool call_ok = false;  // call_edit_section_param() returned true
    std::wstring path = L"(未读取)";
};

void read_path_in_section(void* param, EDIT_SECTION* edit) {
    PathReading* reading = static_cast<PathReading*>(param);
    if (edit == nullptr || edit->get_project_file == nullptr) {
        reading->path = L"(EDIT_SECTION::get_project_file 为空)";
        return;
    }
    reading->path = project_path_of(edit->get_project_file(g_edit));
}

PathReading read_project_path() {
    PathReading reading;
    if (g_edit == nullptr) {
        reading.path = L"(没有 EDIT_HANDLE)";
        return reading;
    }
    reading.call_ok = g_edit->call_edit_section_param(&reading, read_path_in_section);
    return reading;
}

std::wstring describe_reading(const PathReading& reading) {
    if (!reading.call_ok) {
        return L"call_edit_section_param 返回 false (未读取)";
    }
    return reading.path;
}

std::wstring expand_env(const wchar_t* text) {
    wchar_t buffer[1024] = {};
    const DWORD length = ExpandEnvironmentStringsW(text, buffer, 1024);
    if (length == 0 || length > 1024) {
        return L"";
    }
    return buffer;
}

std::wstring format_time(const SYSTEMTIME& st) {
    wchar_t buffer[64] = {};
    std::swprintf(buffer, sizeof(buffer) / sizeof(buffer[0]), L"%04d-%02d-%02d %02d:%02d:%02d",
                  static_cast<int>(st.wYear), static_cast<int>(st.wMonth), static_cast<int>(st.wDay),
                  static_cast<int>(st.wHour), static_cast<int>(st.wMinute), static_cast<int>(st.wSecond));
    return buffer;
}

std::wstring current_time_text() {
    SYSTEMTIME st = {};
    GetLocalTime(&st);
    return format_time(st);
}

struct FileState {
    bool exists = false;
    unsigned long long size = 0;
    std::wstring written;  // local time of last write
};

FileState query_file(const std::wstring& path) {
    FileState state;
    if (path.empty()) {
        return state;
    }
    WIN32_FILE_ATTRIBUTE_DATA data = {};
    if (!GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &data)) {
        return state;
    }
    state.exists = true;
    state.size = (static_cast<unsigned long long>(data.nFileSizeHigh) << 32) | data.nFileSizeLow;
    FILETIME local = {};
    SYSTEMTIME st = {};
    if (FileTimeToLocalFileTime(&data.ftLastWriteTime, &local) && FileTimeToSystemTime(&local, &st)) {
        state.written = format_time(st);
    }
    return state;
}

std::wstring describe_file(const FileState& state) {
    if (!state.exists) {
        return L"不存在";
    }
    return L"存在, " + std::to_wstring(state.size) + L" 字节, 最后写入 " + state.written;
}

bool append_utf8_text(const std::wstring& path, const std::wstring& text) {
    const int length = static_cast<int>(text.size());
    const int needed = WideCharToMultiByte(CP_UTF8, 0, text.c_str(), length, nullptr, 0, nullptr, nullptr);
    if (needed <= 0) {
        return false;
    }
    std::string utf8(static_cast<size_t>(needed), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.c_str(), length, utf8.data(), needed, nullptr, nullptr);

    HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr, OPEN_ALWAYS,
                              FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        return false;
    }
    LARGE_INTEGER zero = {};
    bool ok = SetFilePointerEx(file, zero, nullptr, FILE_END) != FALSE;
    DWORD written = 0;
    ok = ok && WriteFile(file, utf8.data(), static_cast<DWORD>(utf8.size()), &written, nullptr) &&
         written == utf8.size();
    CloseHandle(file);
    return ok;
}

void on_snapshot_menu(void* /*param*/) {
    const std::wstring snapshot_dir = expand_env(kSnapshotDir);
    const std::wstring snapshot_path = expand_env(kSnapshotPath);

    // Step 1: path before the save, and the last load and save callback values.
    const PathReading before = read_project_path();
    std::wstring before_load_path;
    std::wstring before_save_path;
    int before_load_count = 0;
    int before_save_count = 0;
    {
        std::lock_guard<std::mutex> lock(g_state_mutex);
        before_load_path = g_last_load_path;
        before_save_path = g_last_save_path;
        before_load_count = g_load_count;
        before_save_count = g_save_count;
        g_save_count = 0;
        g_last_save_path = L"(本次未回调)";
    }
    const FileState file_before = query_file(snapshot_path);

    // Step 2: make the folder, then save. The save runs outside any edit or read section.
    DWORD dir_error = 0;
    if (!snapshot_dir.empty() && !CreateDirectoryW(snapshot_dir.c_str(), nullptr)) {
        dir_error = GetLastError();
        if (dir_error == ERROR_ALREADY_EXISTS) {
            dir_error = 0;
        }
    }
    const bool save_called = g_edit != nullptr && !snapshot_path.empty();
    const bool save_ok = save_called && g_edit->save_project_file(snapshot_path.c_str());

    // Step 3: count, path and snapshot file after the save.
    int save_count_during = 0;
    std::wstring save_path_during;
    int load_count_after = 0;
    {
        std::lock_guard<std::mutex> lock(g_state_mutex);
        save_count_during = g_save_count;
        save_path_during = g_last_save_path;
        load_count_after = g_load_count;
    }
    const PathReading after = read_project_path();
    const FileState file_after = query_file(snapshot_path);

    std::wstring text;
    text += L"==== SnapshotSaveProbe ";
    text += current_time_text();
    text += L"\n";
    text += L"当前工程路径是否改变: ";
    text += (before.path != after.path) ? L"是" : L"否";
    text += L"\n";
    text += L"保存回调是否触发: ";
    text += (save_count_during > 0) ? L"是" : L"否";
    text += L" (" + std::to_wstring(save_count_during) + L" 次)\n";

    text += L"\n[1] 调用前\n";
    text += L"  路径 (EDIT_SECTION::get_project_file): " + describe_reading(before) + L"\n";
    text += L"  最近一次 load 回调路径 (累计 " + std::to_wstring(before_load_count) + L" 次): " + before_load_path + L"\n";
    text += L"  最近一次 save 回调路径 (累计 " + std::to_wstring(before_save_count) + L" 次): " + before_save_path + L"\n";
    text += L"  快照文件: " + describe_file(file_before) + L"\n";

    text += L"\n[2] save_project_file\n";
    text += L"  目标: " + (snapshot_path.empty() ? std::wstring(L"(%TEMP% 展开失败)") : snapshot_path) + L"\n";
    text += L"  建目录: " +
            (dir_error == 0 ? std::wstring(L"成功或已存在") : L"失败, GetLastError=" + std::to_wstring(dir_error)) +
            L"\n";
    text += L"  是否调用: " + std::wstring(save_called ? L"是" : L"否 (EDIT_HANDLE 为空或路径为空)") + L"\n";
    text += L"  返回值: " + std::wstring(save_called ? (save_ok ? L"true" : L"false") : L"未调用") + L"\n";

    text += L"\n[3] 调用后\n";
    text += L"  路径 (EDIT_SECTION::get_project_file): " + describe_reading(after) + L"\n";
    text += L"  save 回调次数 (本次): " + std::to_wstring(save_count_during) + L"\n";
    text += L"  save 回调看到的路径: " + save_path_during + L"\n";
    text += L"  load 回调累计次数: " + std::to_wstring(load_count_after) + L"\n";
    text += L"  快照文件: " + describe_file(file_after) + L"\n";
    text += L"\n";

    std::wstring result_line = L"结果文件: ";
    if (snapshot_dir.empty()) {
        result_line += L"(%TEMP% 展开失败, 未写入)";
    } else {
        const std::wstring result_path = snapshot_dir + L"\\" + kResultName;
        const bool wrote = append_utf8_text(result_path, text);
        result_line += result_path + (wrote ? L" (已追加)" : L" (写入失败)");
    }

    HWND owner = g_edit != nullptr ? g_edit->get_host_app_window() : nullptr;
    MessageBoxW(owner, (text + result_line).c_str(), kTitle, MB_OK);
}

}  // namespace

extern "C" {

__declspec(dllexport) COMMON_PLUGIN_TABLE* GetCommonPluginTable() {
    static COMMON_PLUGIN_TABLE table = {
        L"SnapshotSaveProbe",
        L"Test-only probe: does save_project_file() change the current project path and fire the save callback?",
    };
    return &table;
}

__declspec(dllexport) void RegisterPlugin(HOST_APP_TABLE* host) {
    if (host == nullptr) {
        return;
    }
    g_edit = host->create_edit_handle();
    host->register_project_load_handler(on_project_load);
    host->register_project_save_handler(on_project_save);
    host->register_edit_menu_param(kMenuName, nullptr, on_snapshot_menu);
}

}  // extern "C"
