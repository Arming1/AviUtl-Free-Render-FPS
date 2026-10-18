// Free FPS Export: part of x264guiEx-FreeRenderFPS.aux2. It exports the current project at a freely chosen output
// frame rate, using public SDK calls only.
//
// Menu: Edit > Free FPS Export...
//
// The output plugin is this DLL's own output plugin (its registered name, see RegisterPlugin). The menu has no
// plugin field.
//
// Flow (one run at a time, driven by a 100 ms thread timer on the host main thread):
//   1. Project path: taken from the project load and save callbacks (PROJECT_FILE::get_project_file_path,
//      plugin2.h). A project with no path is refused: save it first.
//   2. Dialog: target FPS (exact rational: 66, 60000/1001; decimals refused), output file, and the precise motion
//      option (always on; shown only in debug mode, see DebugConfig; linear trackbars are re-sampled exactly, see
//      retime.h), and the sub-scene rate policy
//      (auto by default; SubsceneRatePolicy in retime.h). A read-only scene plan shows what each scene will become: a
//      dry run of retime() on the saved project file, nothing written. The encoder settings button opens the
//      x264guiEx settings window (the output settings, edited in place); a read-only summary shows them.
//   3. Confirm: the SDK has no unsaved-changes flag, so the user is always asked whether to save the project to its
//      own path first. No (or closing the prompt) aborts with no side effects.
//   4. save_project_file(original) once confirmed, then copy the saved original to
//      %TEMP%\FreeFpsExport\<timestamp>\src.aup2 (a file copy, not a save to temp).
//   5. retime() writes retimed.aup2 and retimed.aup2.convert.json beside it, in the same temp folder.
//   6. open_project_file(retimed.aup2, false), then output_file(out, <own plugin name>, nullptr, nullptr).
//   7. Wait for CHANGE_EDIT_STATE (SAVE, then EDIT) and check the output file.
//   8. open_project_file(original, false) on every outcome once the user has confirmed. The original project is
//      the one reopened, never the temp copy.
//   9. Delete the temp folder. FREE_FPS_EXPORT_KEEP_TEMP=1 keeps it for debugging. The debug ini (keep_temp=1) keeps
//      retimed.aup2 and its sidecar only.
//
// Separate-process mode (dialog check box; default on when the saved project uses SceneMotionBlur_K, whose
// unload crashes AviUtl2 when the project is switched). The running host never switches project. Steps 1 to 5 are the
// same, then: the plugin writes job.ini beside retimed.aup2 and starts a second aviutl2.exe (this host's own exe) with
// the environment variable FREE_FPS_EXPORT_JOB=<job.ini>. In that process this DLL opens retimed.aup2, runs
// output_file, writes result.ini and ends the process. The first host only polls the process handle, reads
// result.ini, checks the output file again, and shows the result. The original project is neither closed nor reopened.
//
// Threading: as in experiments/auto_export. SDK calls run on the main thread (menu callback and timer). The
// CHANGE_EDIT_STATE listener only sets an atomic flag. The project load and save handlers only store the path.
//
// Entry points exported by this DLL for this part: GetCommonPluginTable, RegisterPlugin, UninitializePlugin.
// InitializeLogger is defined in x264guiEx.cpp, which forwards the logger to freefps_export_set_logger().
// The output plugin table is registered by RegisterPlugin; GetOutputPluginTable is not exported from the .aux2.

#include <windows.h>
#include <commdlg.h>
#include <wchar.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "auo_mes.h"
#include "auo_version.h"
#include "logger2.h"
#include "output2.h"
#include "plugin2.h"
#include "retime.h"
#include "freefps_export.h"

#pragma comment(lib, "user32.lib")     // SetTimer, MessageBoxW, dialogs
#pragma comment(lib, "gdi32.lib")      // text measurement for the dialog layout (SelectObject, GetStockObject)
#pragma comment(lib, "comdlg32.lib")   // GetSaveFileNameW

std::atomic<bool> g_freefps_menu_export_active{false};  // see freefps_export.h
std::atomic<bool> g_freefps_child_process{false};       // see freefps_export.h

namespace {

using free_fps::Rational;
using free_fps::RetimeResult;

// Time limits in milliseconds. The host-stub test overrides these to keep runs short.
struct Timing {
    DWORD tick_ms = 100;
    ULONGLONG ready_timeout_ms = 60000;         // edit state must return to EDIT
    ULONGLONG load_timeout_ms = 30000;          // project load callback after open_project_file
    ULONGLONG export_start_timeout_ms = 30000;  // CHANGE_EDIT_STATE(SAVE) after output_file
    ULONGLONG export_timeout_ms = 3600000;      // whole export
    ULONGLONG child_start_timeout_ms = 90000;   // second host: startup until the export timer first runs
    ULONGLONG child_grace_ms = 120000;          // first host: extra wait for the second host (startup + report)
};

Timing g_timing;

constexpr UINT_PTR kTimerId = 0x4646;
constexpr wchar_t kTitle[] = L"Free FPS Export";
constexpr wchar_t kMenuName[] = L"Free FPS Export...";
constexpr wchar_t kKeepTempEnv[] = L"FREE_FPS_EXPORT_KEEP_TEMP";
constexpr wchar_t kSrcName[] = L"src.aup2";
constexpr wchar_t kRetimedName[] = L"retimed.aup2";
constexpr wchar_t kSidecarName[] = L".convert.json";
constexpr wchar_t kJobName[] = L"job.ini";
constexpr wchar_t kResultName[] = L"result.ini";
constexpr wchar_t kJobEnv[] = L"FREE_FPS_EXPORT_JOB";
constexpr wchar_t kJobSection[] = L"job";
constexpr wchar_t kResultSection[] = L"result";
constexpr char kSceneMotionBlurMarker[] = "effect.name=SceneMotionBlur_K";
// FAT volumes store write times with 2 s resolution. The export must have written the file at or after
// start minus this slack.
constexpr LONGLONG kWriteTimeSlack100ns = 20000000LL;

constexpr int kStateEdit = EDIT_HANDLE::EDIT_STATE_EDIT;
constexpr int kStateSave = EDIT_HANDLE::EDIT_STATE_SAVE;

// Dialog control IDs.
constexpr int kIdRate = 101;
constexpr int kIdOutput = 102;
constexpr int kIdBrowse = 103;
constexpr int kIdPrecise = 104;
constexpr int kIdRateLabel = 105;
constexpr int kIdOutputLabel = 106;
constexpr int kIdSummary = 107;
constexpr int kIdEncSettings = 108;
constexpr int kIdSubsceneLabel = 109;
constexpr int kIdSubsceneCombo = 110;
constexpr int kIdScenePlan = 111;
constexpr int kIdSeparate = 112;
constexpr int kIdSeparateHint = 113;

// Dialog metrics in dialog units. The content width is kBaseContentWidth, or wider when a text needs it (layout_dialog).
constexpr int kMargin = 7;
constexpr int kBaseContentWidth = 248;
constexpr int kDialogHeight = 140;  // initial size of the template; layout_dialog sets the real one
constexpr int kButtonPadding = 12;  // added to a button's text width
constexpr int kCheckPadding = 14;   // check box square and spacing
// A combo box's height is the height of its drop-down list, so the box reserves room for the three policy items.
constexpr int kSubsceneComboHeight = 38;

LOG_HANDLE* g_logger = nullptr;
EDIT_HANDLE* g_edit = nullptr;
HINSTANCE g_instance = nullptr;
// Registered name of this DLL's output plugin. Set by RegisterPlugin from the table that was registered.
std::wstring g_output_name;

// Set by the CHANGE_EDIT_STATE listener (notification thread) or by polling in the state machine.
std::atomic<bool> g_save_seen{false};
// Incremented by the project load handler.
std::atomic<unsigned> g_load_seq{0};
std::mutex g_path_mutex;
std::wstring g_loaded_path;   // path reported by the last project load callback (guarded by g_path_mutex)
std::wstring g_project_path;  // current project path from the load and save callbacks ("" = none)

// Root of the temp folders. Empty means %TEMP%\FreeFpsExport\. The host-stub test replaces it.
std::wstring g_work_root;

// Shows the final result. Replaced by the host-stub test so that no message box opens during tests.
using NotifyFn = void (*)(bool ok, const std::wstring& text);
// Asks whether to save the project to its own path before the export. Replaced by the host-stub test.
using ConfirmFn = bool (*)(const std::wstring& path);

void show_message(bool ok, const std::wstring& text) {
    HWND parent = g_edit != nullptr ? g_edit->get_host_app_window() : nullptr;
    MessageBoxW(parent, text.c_str(), kTitle, MB_OK | (ok ? MB_ICONINFORMATION : MB_ICONERROR));
}

bool show_save_prompt(const std::wstring& path) {
    HWND parent = g_edit != nullptr ? g_edit->get_host_app_window() : nullptr;
    const std::wstring text = L"Free FPS Export needs the project saved first.\n\nSave the project to:\n" + path +
                              L"\n\nand export the saved file?";
    return MessageBoxW(parent, text.c_str(), kTitle, MB_YESNO | MB_ICONQUESTION) == IDYES;
}

NotifyFn g_notify = show_message;
ConfirmFn g_confirm_save = show_save_prompt;

void log(bool error, const std::wstring& message) {
    if (g_logger == nullptr) {
        return;
    }
    const std::wstring text = std::wstring(L"[FreeFpsExport] ") + message;
    if (error) {
        g_logger->error(g_logger, text.c_str());
    } else {
        g_logger->info(g_logger, text.c_str());
    }
}

std::wstring to_wide(const std::string& text) {
    std::wstring out;
    if (!free_fps::utf8_to_wide(text, out)) {
        out = L"(invalid UTF-8)";
    }
    return out;
}

std::wstring current_project_path() {
    std::lock_guard<std::mutex> lock(g_path_mutex);
    return g_project_path;
}

bool read_file_bytes(const std::wstring& path, std::string& bytes) {
    bytes.clear();
    HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                              OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        return false;
    }
    LARGE_INTEGER size{};
    bool ok = GetFileSizeEx(file, &size) && size.QuadPart <= (1LL << 30);
    if (ok && size.QuadPart > 0) {
        bytes.resize(static_cast<std::size_t>(size.QuadPart));
        DWORD read = 0;
        ok = ReadFile(file, &bytes[0], static_cast<DWORD>(bytes.size()), &read, nullptr) && read == bytes.size();
    }
    CloseHandle(file);
    return ok;
}

bool write_file_bytes(const std::wstring& path, const std::string& bytes) {
    HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL,
                              nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        return false;
    }
    DWORD written = 0;
    const bool ok = bytes.empty() || (WriteFile(file, bytes.data(), static_cast<DWORD>(bytes.size()), &written,
                                                nullptr) && written == bytes.size());
    CloseHandle(file);
    return ok;
}

bool keep_temp_requested() {
    wchar_t value[4] = {};
    const DWORD length = GetEnvironmentVariableW(kKeepTempEnv, value, 4);
    return length == 1 && value[0] == L'1';
}

// Debug settings from free_fps_export.ini, in the folder of this DLL (see the .example in test_kit). Without the file,
// or without [debug] enabled=1, the normal mode applies: precise motion on and hidden, no extra decimals, temp files
// removed.
constexpr wchar_t kDebugIniName[] = L"free_fps_export.ini";
constexpr wchar_t kDebugSection[] = L"debug";
constexpr int kMaxExtraDecimals = 4;

struct DebugConfig {
    bool enabled = false;
    bool precise = true;     // [debug] precise, default 1; the dialog shows the check box only in debug mode
    int extra_decimals = 0;  // [debug] extra_decimals, clamped to 0..4
    bool keep_temp = false;  // [debug] keep_temp = 1 keeps retimed.aup2 and its sidecar after the export
};

// Path of free_fps_export.ini beside this DLL. g_instance is set by RegisterPlugin. Empty on failure.
std::wstring debug_ini_path() {
    if (g_instance == nullptr) {
        return std::wstring();
    }
    std::vector<wchar_t> module(32768, L'\0');
    const DWORD length = GetModuleFileNameW(g_instance, module.data(), static_cast<DWORD>(module.size()));
    if (length == 0 || length >= module.size()) {
        return std::wstring();
    }
    const std::wstring path(module.data(), length);
    const std::size_t slash = path.find_last_of(L'\\');
    if (slash == std::wstring::npos) {
        return std::wstring();
    }
    return path.substr(0, slash + 1) + kDebugIniName;
}

DebugConfig load_debug_config() {
    DebugConfig config;
    const std::wstring path = debug_ini_path();
    if (path.empty() || GetPrivateProfileIntW(kDebugSection, L"enabled", 0, path.c_str()) != 1) {
        return config;
    }
    config.enabled = true;
    config.precise = GetPrivateProfileIntW(kDebugSection, L"precise", 1, path.c_str()) != 0;
    const int decimals = GetPrivateProfileIntW(kDebugSection, L"extra_decimals", 0, path.c_str());
    config.extra_decimals = (std::min)(kMaxExtraDecimals, (std::max)(0, decimals));
    config.keep_temp = GetPrivateProfileIntW(kDebugSection, L"keep_temp", 0, path.c_str()) == 1;
    return config;
}

// Creates <root><yyyymmdd-hhmmss-mmm>[-n]\ and returns its path with a trailing backslash. Root defaults to
// %TEMP%\FreeFpsExport\. Empty string on failure.
std::wstring make_work_dir() {
    std::wstring base = g_work_root;
    if (base.empty()) {
        wchar_t temp[MAX_PATH + 1] = {};
        if (GetTempPathW(MAX_PATH + 1, temp) == 0) {
            return std::wstring();
        }
        base = std::wstring(temp) + L"FreeFpsExport\\";
    }
    CreateDirectoryW(base.c_str(), nullptr);  // ERROR_ALREADY_EXISTS is fine
    SYSTEMTIME now{};
    GetLocalTime(&now);
    wchar_t stamp[64] = {};
    swprintf_s(stamp, L"%04u%02u%02u-%02u%02u%02u-%03u", now.wYear, now.wMonth, now.wDay, now.wHour, now.wMinute,
               now.wSecond, now.wMilliseconds);
    for (int n = 0; n < 100; ++n) {
        std::wstring dir = base + stamp;
        if (n > 0) {
            dir += L"-" + std::to_wstring(n);
        }
        if (CreateDirectoryW(dir.c_str(), nullptr)) {
            return dir + L"\\";
        }
        if (GetLastError() != ERROR_ALREADY_EXISTS) {
            break;
        }
    }
    return std::wstring();
}

bool file_exists(const std::wstring& path) {
    return GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES;
}

// Deletes only the files this plugin writes, then the folder. Files that cannot be deleted (for example a file the
// host still holds) are logged and left in place. With keep_outputs (debug keep_temp), retimed.aup2 and its sidecar
// stay, so the folder stays too; only the source copy is deleted.
void remove_work_dir(const std::wstring& dir, bool keep_outputs) {
    if (dir.empty()) {
        return;
    }
    std::vector<std::wstring> names = {kSrcName};
    if (!keep_outputs) {
        names.push_back(kRetimedName);
        names.push_back(std::wstring(kRetimedName) + kSidecarName);
        names.push_back(kJobName);
        names.push_back(kResultName);
    }
    for (const std::wstring& name : names) {
        if (!DeleteFileW((dir + name).c_str()) && GetLastError() != ERROR_FILE_NOT_FOUND) {
            log(true, L"cannot delete temp file " + dir + name);
        }
    }
    // With keep_outputs the folder is not empty when files were kept; that failure is expected and not logged.
    if (!RemoveDirectoryW(dir.c_str()) && !keep_outputs) {
        log(true, L"cannot remove temp folder " + dir);
    }
}

// Returns true if the output file exists, has a size above zero, and was written at or after since.
bool output_written(const std::wstring& path, const FILETIME& since, long long& bytes, std::wstring& why) {
    WIN32_FILE_ATTRIBUTE_DATA data{};
    if (!GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &data)) {
        bytes = -1;
        why = L"output file missing after export";
        return false;
    }
    LARGE_INTEGER size{};
    size.HighPart = static_cast<LONG>(data.nFileSizeHigh);
    size.LowPart = data.nFileSizeLow;
    bytes = size.QuadPart;
    if (bytes <= 0) {
        why = L"output file missing or empty after export";
        return false;
    }
    ULARGE_INTEGER written{};
    written.LowPart = data.ftLastWriteTime.dwLowDateTime;
    written.HighPart = data.ftLastWriteTime.dwHighDateTime;
    ULARGE_INTEGER start{};
    start.LowPart = since.dwLowDateTime;
    start.HighPart = since.dwHighDateTime;
    const ULONGLONG slack = static_cast<ULONGLONG>(kWriteTimeSlack100ns);
    const ULONGLONG floor = start.QuadPart > slack ? start.QuadPart - slack : 0;
    if (written.QuadPart < floor) {
        why = L"output file was not written by this export (its timestamp is older than the start)";
        return false;
    }
    return true;
}

// ---------------------------------------------------------------------------
// Export run state machine
// ---------------------------------------------------------------------------

struct ExportRequest {
    Rational target;
    bool precise = true;  // always true outside debug mode
    free_fps::SubsceneRatePolicy policy = free_fps::SubsceneRatePolicy::Auto;
    std::size_t extra_decimals = 0;  // precise mode only; 0 outside debug mode
    bool keep_outputs = false;       // debug keep_temp: keep retimed.aup2 and its sidecar
    bool separate_process = false;   // export in a second host process, no project switch in this one
    std::wstring output;
    std::wstring plugin;
    std::wstring original;  // the project file path, set by begin_export()
};

enum class Phase {
    kWaitReady, kSave, kOpen, kWaitLoad, kStart, kWaitExport, kRestore, kWaitRestoreLoad, kDone,
    kSpawn, kWaitChild  // separate-process mode only
};

struct Run {
    ExportRequest request;
    std::wstring work_dir;
    std::wstring src_path;
    std::wstring retimed_path;
    std::wstring json_path;
    std::wstring job_path;     // separate-process mode: job.ini and result.ini in work_dir
    std::wstring result_path;
    HANDLE child = nullptr;    // separate-process mode: the second host process
    std::wstring child_encoder;  // encoder summary the second host reported
    Phase phase = Phase::kWaitReady;
    ULONGLONG phase_started = 0;
    unsigned load_seq_before = 0;
    bool export_ok = false;
    long long output_bytes = -1;
    FILETIME export_since{};
    std::wstring failure;      // first failure reason
    std::wstring restore_note;
    std::wstring rate_text;
    bool done = false;
};

std::unique_ptr<Run> g_run;

void enter(Run& run, Phase phase) {
    run.phase = phase;
    run.phase_started = GetTickCount64();
}

// Records the first failure. The original project is reopened afterwards, on every outcome once confirmed. In
// separate-process mode this host never switched project, so there is nothing to reopen.
void fail(Run& run, const std::wstring& reason) {
    if (run.failure.empty()) {
        run.failure = reason;
    }
    log(true, reason);
    enter(run, run.request.separate_process ? Phase::kDone : Phase::kRestore);
}

std::wstring rate_label(const Rational& r) {
    return std::to_wstring(r.num) + L"/" + std::to_wstring(r.den);
}

// ---- job.ini and result.ini (separate-process mode) ----
// Both are UTF-16 ini files: created with a BOM so that Get/WritePrivateProfileStringW keep non-ANSI path characters.

bool create_unicode_ini(const std::wstring& path) {
    return write_file_bytes(path, std::string("\xFF\xFE", 2));
}

bool ini_put(const std::wstring& file, const wchar_t* section, const wchar_t* key, const std::wstring& value) {
    std::wstring flat = value;
    std::replace(flat.begin(), flat.end(), L'\r', L' ');
    std::replace(flat.begin(), flat.end(), L'\n', L' ');
    return WritePrivateProfileStringW(section, key, flat.c_str(), file.c_str()) != FALSE;
}

std::wstring ini_get(const std::wstring& file, const wchar_t* section, const wchar_t* key) {
    std::vector<wchar_t> buffer(8192, L'\0');
    GetPrivateProfileStringW(section, key, L"", buffer.data(), static_cast<DWORD>(buffer.size()), file.c_str());
    return std::wstring(buffer.data());
}

// True if the project text has an effect named SceneMotionBlur_K (the whole name, not a longer one).
bool uses_scene_motion_blur(const std::string& bytes) {
    const std::string marker = kSceneMotionBlurMarker;
    for (std::size_t at = bytes.find(marker); at != std::string::npos; at = bytes.find(marker, at + 1)) {
        const std::size_t end = at + marker.size();
        if (end >= bytes.size() || bytes[end] == '\r' || bytes[end] == '\n') {
            return true;
        }
    }
    return false;
}

// This process's environment plus FREE_FPS_EXPORT_JOB=<job>, as a double-NUL terminated block for CreateProcessW.
std::vector<wchar_t> make_child_environment(const std::wstring& job_path) {
    std::vector<wchar_t> block;
    wchar_t* env = GetEnvironmentStringsW();
    if (env == nullptr) {
        return block;
    }
    const std::size_t key_length = wcslen(kJobEnv);
    for (const wchar_t* entry = env; *entry != L'\0';) {
        const std::size_t length = wcslen(entry);
        const bool stale = length > key_length && _wcsnicmp(entry, kJobEnv, key_length) == 0 && entry[key_length] == L'=';
        if (!stale) {
            block.insert(block.end(), entry, entry + length + 1);
        }
        entry += length + 1;
    }
    FreeEnvironmentStringsW(env);
    const std::wstring added = std::wstring(kJobEnv) + L"=" + job_path;
    block.insert(block.end(), added.c_str(), added.c_str() + added.size() + 1);
    block.push_back(L'\0');
    return block;
}

// Starts the host's own exe (GetModuleFileNameW(nullptr)) with the job in the environment. Sets run.child.
bool spawn_child(Run& run, std::wstring& why) {
    std::vector<wchar_t> module(32768, L'\0');
    const DWORD length = GetModuleFileNameW(nullptr, module.data(), static_cast<DWORD>(module.size()));
    if (length == 0 || length >= module.size()) {
        why = L"cannot find the host executable";
        return false;
    }
    const std::wstring exe(module.data(), length);
    std::wstring command_line = L"\"" + exe + L"\"";
    std::vector<wchar_t> command(command_line.begin(), command_line.end());
    command.push_back(L'\0');
    std::vector<wchar_t> environment = make_child_environment(run.job_path);
    if (environment.empty()) {
        why = L"cannot build the environment for the export process";
        return false;
    }
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION info{};
    if (!CreateProcessW(exe.c_str(), command.data(), nullptr, nullptr, FALSE, CREATE_UNICODE_ENVIRONMENT,
                        environment.data(), nullptr, &startup, &info)) {
        why = L"cannot start the export process (error " + std::to_wstring(GetLastError()) + L"): " + exe;
        return false;
    }
    CloseHandle(info.hThread);
    run.child = info.hProcess;
    log(false, L"export process started: " + exe);
    return true;
}

// Ends the second host if it still runs, and closes the handle.
void release_child(Run& run) {
    if (run.child == nullptr) {
        return;
    }
    if (WaitForSingleObject(run.child, 0) == WAIT_TIMEOUT) {
        TerminateProcess(run.child, 1);
        WaitForSingleObject(run.child, 5000);  // let it release its files
    }
    CloseHandle(run.child);
    run.child = nullptr;
}

// The second host has ended: read result.ini and check the output file again.
void collect_child_result(Run& run, DWORD exit_code) {
    const bool has_result = file_exists(run.result_path);
    const std::wstring reported_ok = ini_get(run.result_path, kResultSection, L"ok");
    const std::wstring error = ini_get(run.result_path, kResultSection, L"error");
    run.child_encoder = ini_get(run.result_path, kResultSection, L"encoder");
    if (reported_ok == L"1") {
        std::wstring why;
        if (output_written(run.request.output, run.export_since, run.output_bytes, why)) {
            run.export_ok = true;
            log(false, L"export OK (separate process): " + run.request.output);
        } else {
            run.failure = L"the export process reported success, but " + why;
            log(true, run.failure);
        }
        return;
    }
    if (!has_result) {
        run.failure = L"the export process ended (exit code " + std::to_wstring(exit_code) +
                      L") without writing a result";
    } else {
        run.failure = L"the export process failed: " + (error.empty() ? std::wstring(L"(no reason given)") : error);
    }
    log(true, run.failure);
}

void finish_run() {
    KillTimer(nullptr, kTimerId);
    // Every end of a run comes through here (success, failure, timeout, restore). Clear the menu-export flag first.
    g_freefps_menu_export_active = false;
    // Move the run out before notifying: the message box pumps messages, and the timer must not re-enter.
    std::unique_ptr<Run> run = std::move(g_run);
    if (!run) {
        return;
    }
    const bool ok = run->export_ok;
    std::wstring text;
    if (ok) {
        text = L"Export finished.\nOutput: " + run->request.output + L"\nSize: " +
               std::to_wstring(run->output_bytes) + L" bytes\nRate: " + run->rate_text;
        if (run->request.separate_process) {
            text += L"\nExported in a separate process.";
            if (!run->child_encoder.empty()) {
                text += L"\nEncoder: " + run->child_encoder;
            }
        }
    } else {
        text = L"Export failed.\n" + run->failure;
    }
    if (!run->restore_note.empty()) {
        text += L"\n\n" + run->restore_note;
    }
    release_child(*run);  // the temp files below are deleted only once the second host no longer holds them
    const bool keep = keep_temp_requested();
    if (keep) {
        text += L"\n\nTemp files kept (" + std::wstring(kKeepTempEnv) + L"=1): " + run->work_dir;
    } else if (run->request.keep_outputs) {
        remove_work_dir(run->work_dir, true);
        std::wstring kept;
        for (const std::wstring& path : {run->retimed_path, run->json_path}) {
            if (file_exists(path)) {
                kept += L"\n" + path;
            }
        }
        if (!kept.empty()) {
            text += L"\n\nDebug keep_temp=1, kept:" + kept;
        }
    } else {
        remove_work_dir(run->work_dir, false);
    }
    log(!ok, L"run finished: " + std::wstring(ok ? L"OK" : L"FAILED"));
    g_notify(ok, text);
}

void tick(Run& run) {
    const ULONGLONG now = GetTickCount64();
    const ULONGLONG waited = now - run.phase_started;
    const int state = g_edit->get_edit_state();
    const std::wstring& original = run.request.original;

    switch (run.phase) {
    case Phase::kWaitReady:
        if (state == kStateEdit) {
            enter(run, Phase::kSave);
        } else if (waited > g_timing.ready_timeout_ms) {
            fail(run, L"the host did not return to the edit state before the export");
        }
        return;

    case Phase::kSave: {
        if (!g_edit->save_project_file(original.c_str())) {
            fail(run, L"save_project_file returned false");
            return;
        }
        if (!CopyFileW(original.c_str(), run.src_path.c_str(), FALSE)) {
            fail(run, L"cannot copy the saved project to " + run.src_path);
            return;
        }
        std::string src_bytes;
        if (!read_file_bytes(run.src_path, src_bytes)) {
            fail(run, L"cannot read the temp copy: " + run.src_path);
            return;
        }
        const RetimeResult result = free_fps::retime(src_bytes, run.request.target, run.retimed_path,
                                                     run.request.precise, run.request.extra_decimals,
                                                     run.request.policy);
        if (!result.ok) {
            fail(run, to_wide(free_fps::refusal_message(result)));
            return;
        }
        if (!write_file_bytes(run.retimed_path, result.output)) {
            fail(run, L"cannot write " + run.retimed_path);
            return;
        }
        if (!write_file_bytes(run.json_path, free_fps::sidecar_json(result, run.src_path, run.retimed_path))) {
            fail(run, L"cannot write " + run.json_path);
            return;
        }
        log(false, L"retimed to " + run.rate_text + L": " + run.retimed_path);
        enter(run, run.request.separate_process ? Phase::kSpawn : Phase::kOpen);
        return;
    }

    case Phase::kOpen:
        run.load_seq_before = g_load_seq.load();
        if (!g_edit->open_project_file(run.retimed_path.c_str(), false)) {
            fail(run, L"open_project_file returned false");
            return;
        }
        enter(run, Phase::kWaitLoad);
        return;

    case Phase::kWaitLoad:
        if (g_load_seq.load() != run.load_seq_before) {
            std::wstring loaded;
            {
                std::lock_guard<std::mutex> lock(g_path_mutex);
                loaded = g_loaded_path;
            }
            if (_wcsicmp(loaded.c_str(), run.retimed_path.c_str()) != 0) {
                fail(run, L"the loaded project differs from the retimed copy: " + loaded);
                return;
            }
            enter(run, Phase::kStart);
        } else if (waited > g_timing.load_timeout_ms) {
            fail(run, L"the project load callback was not observed");
        }
        return;

    case Phase::kStart: {
        g_save_seen = false;
        FILETIME now_ft{};
        GetSystemTimeAsFileTime(&now_ft);
        run.export_since = now_ft;
        g_freefps_menu_export_active = true;  // cleared in finish_run(), on every outcome
        if (!g_edit->output_file(run.request.output.c_str(), run.request.plugin.c_str(), nullptr, nullptr)) {
            fail(run, L"output_file returned false");
            return;
        }
        enter(run, Phase::kWaitExport);
        return;
    }

    case Phase::kWaitExport:
        if (state == kStateSave) {
            g_save_seen = true;
        }
        if (g_save_seen && state == kStateEdit) {
            std::wstring why;
            if (output_written(run.request.output, run.export_since, run.output_bytes, why)) {
                run.export_ok = true;
                log(false, L"export OK: " + run.request.output);
                enter(run, Phase::kRestore);
            } else {
                fail(run, why);
            }
        } else if (!g_save_seen && waited > g_timing.export_start_timeout_ms) {
            fail(run, L"the export start (CHANGE_EDIT_STATE) was not observed");
        } else if (waited > g_timing.export_timeout_ms) {
            fail(run, L"the export timed out");
        }
        return;

    case Phase::kRestore:
        if (state != kStateEdit) {
            if (waited > g_timing.ready_timeout_ms) {
                run.restore_note = L"The original project was not reopened: the host did not return to the edit state.";
                enter(run, Phase::kDone);
            }
            return;
        }
        run.load_seq_before = g_load_seq.load();
        if (!g_edit->open_project_file(original.c_str(), false)) {
            run.restore_note = L"The original project was not reopened: open_project_file returned false for " +
                               original;
            enter(run, Phase::kDone);
            return;
        }
        enter(run, Phase::kWaitRestoreLoad);
        return;

    case Phase::kWaitRestoreLoad:
        if (g_load_seq.load() != run.load_seq_before) {
            std::wstring loaded;
            {
                std::lock_guard<std::mutex> lock(g_path_mutex);
                loaded = g_loaded_path;
            }
            run.restore_note = _wcsicmp(loaded.c_str(), original.c_str()) == 0
                                   ? L"The original project was reopened: " + original
                                   : L"The original project was not reopened: the loaded project is " + loaded;
            enter(run, Phase::kDone);
        } else if (waited > g_timing.load_timeout_ms) {
            run.restore_note = L"The original project was not reopened: the project load callback was not observed.";
            enter(run, Phase::kDone);
        }
        return;

    case Phase::kSpawn: {
        // The job: everything the second host needs. The encoder settings travel inside retimed.aup2
        // (freerenderfps_config=, kept by retime()), not here.
        std::wstring why;
        if (!create_unicode_ini(run.job_path) ||
            !ini_put(run.job_path, kJobSection, L"retimed", run.retimed_path) ||
            !ini_put(run.job_path, kJobSection, L"output", run.request.output) ||
            !ini_put(run.job_path, kJobSection, L"plugin", run.request.plugin) ||
            !ini_put(run.job_path, kJobSection, L"original", original) ||
            !ini_put(run.job_path, kJobSection, L"rate", run.rate_text) ||
            !ini_put(run.job_path, kJobSection, L"timeout_ms", std::to_wstring(g_timing.export_timeout_ms))) {
            fail(run, L"cannot write " + run.job_path);
            return;
        }
        DeleteFileW(run.result_path.c_str());
        GetSystemTimeAsFileTime(&run.export_since);
        if (!spawn_child(run, why)) {
            fail(run, why);
            return;
        }
        enter(run, Phase::kWaitChild);
        return;
    }

    case Phase::kWaitChild:
        if (WaitForSingleObject(run.child, 0) == WAIT_OBJECT_0) {
            DWORD exit_code = 0;
            GetExitCodeProcess(run.child, &exit_code);
            collect_child_result(run, exit_code);
            enter(run, Phase::kDone);
        } else if (waited > g_timing.export_timeout_ms + g_timing.child_grace_ms) {
            run.failure = L"the export process timed out";  // release_child() in finish_run() ends it
            log(true, run.failure);
            enter(run, Phase::kDone);
        }
        return;

    case Phase::kDone:
        run.done = true;
        return;
    }
}

void CALLBACK on_timer(HWND, UINT, UINT_PTR, DWORD) {
    if (!g_run) {
        KillTimer(nullptr, kTimerId);
        return;
    }
    tick(*g_run);
    if (g_run && g_run->done) {
        finish_run();
    }
}

// Starts a run. work_dir must already exist and end with a backslash. Returns false if it cannot start.
bool start_export(const ExportRequest& request, const std::wstring& work_dir) {
    if (g_run) {
        log(true, L"an export is already running");
        return false;
    }
    if (g_edit == nullptr) {
        return false;
    }
    auto run = std::make_unique<Run>();
    run->request = request;
    run->work_dir = work_dir;
    run->src_path = work_dir + kSrcName;
    run->retimed_path = work_dir + kRetimedName;
    run->json_path = run->retimed_path + kSidecarName;
    run->job_path = work_dir + kJobName;
    run->result_path = work_dir + kResultName;
    run->rate_text = rate_label(request.target);
    if (SetTimer(nullptr, kTimerId, g_timing.tick_ms, on_timer) == 0) {
        log(true, L"cannot start the export timer");
        return false;
    }
    log(false, L"export " + run->rate_text + L" -> " + request.output);
    g_run = std::move(run);
    return true;
}

// Checks the project path, asks the user to confirm the save, creates the temp folder and starts the run.
// Refusals before the confirmation have no side effects. Cancelling the prompt has none either.
bool begin_export(ExportRequest request) {
    if (g_run) {
        g_notify(false, L"An export is already running.");
        return false;
    }
    if (g_edit == nullptr) {
        return false;
    }
    request.original = current_project_path();
    if (request.original.empty()) {
        g_notify(false, L"The project has no file yet. Save it first, then run Free FPS Export again.");
        return false;
    }
    if (!g_confirm_save(request.original)) {
        log(false, L"export cancelled by the user");
        return false;
    }
    const std::wstring work_dir = make_work_dir();
    if (work_dir.empty()) {
        g_notify(false, L"Cannot create the temp folder for the export.");
        return false;
    }
    return start_export(request, work_dir);
}

// ---------------------------------------------------------------------------
// Second host process (FREE_FPS_EXPORT_JOB set): open retimed.aup2, export, write result.ini, end the process.
// ---------------------------------------------------------------------------
// Started from the first project load callback (see on_project_load): the host has finished its own start-up by then,
// and the timer is created on the thread that runs the host's message loop for project events. RegisterPlugin only
// records the job and starts a watchdog thread that ends the process with an error if the timer never runs, or the
// export takes too long. The process ends with TerminateProcess on itself, so no plugin (SceneMotionBlur_K) runs its
// unload code, and nothing is written to the user's host settings.

struct ChildJob {
    bool active = false;
    std::wstring job_path;
    std::wstring result_path;
    std::wstring retimed;
    std::wstring output;
    std::wstring plugin;
    ULONGLONG timeout_ms = 3600000;
};

enum class ChildPhase { kWaitReady, kOpen, kWaitLoad, kStart, kWaitExport };

ChildJob g_child_job;
ChildPhase g_child_phase = ChildPhase::kWaitReady;
ULONGLONG g_child_phase_started = 0;
unsigned g_child_load_seq_before = 0;
FILETIME g_child_export_since{};
std::wstring g_child_encoder;
std::atomic<bool> g_child_timer_started{false};
std::atomic<bool> g_child_ticked{false};
std::atomic<bool> g_child_result_written{false};

// Writes result.ini (once) and ends the process. Whoever gets here first writes; a second caller waits to be ended.
void child_finish(bool ok, const std::wstring& error, long long bytes) {
    if (g_child_result_written.exchange(true)) {
        Sleep(INFINITE);
    }
    const std::wstring& path = g_child_job.result_path;
    create_unicode_ini(path);
    ini_put(path, kResultSection, L"ok", ok ? L"1" : L"0");
    ini_put(path, kResultSection, L"output", g_child_job.output);
    ini_put(path, kResultSection, L"bytes", std::to_wstring(bytes));
    ini_put(path, kResultSection, L"encoder", g_child_encoder);
    ini_put(path, kResultSection, L"error", error);
    log(!ok, std::wstring(L"export process finished: ") + (ok ? L"OK" : L"FAILED " + error));
    TerminateProcess(GetCurrentProcess(), ok ? 0 : 1);
    Sleep(INFINITE);
}

void child_fail(const std::wstring& reason) {
    child_finish(false, reason, -1);
}

void child_enter(ChildPhase phase) {
    g_child_phase = phase;
    g_child_phase_started = GetTickCount64();
}

void child_tick() {
    g_child_ticked = true;
    const ULONGLONG waited = GetTickCount64() - g_child_phase_started;
    const int state = g_edit->get_edit_state();

    switch (g_child_phase) {
    case ChildPhase::kWaitReady:
        if (state == kStateEdit) {
            child_enter(ChildPhase::kOpen);
        } else if (waited > g_timing.ready_timeout_ms) {
            child_fail(L"the host did not return to the edit state before the export");
        }
        return;

    case ChildPhase::kOpen:
        g_child_load_seq_before = g_load_seq.load();
        if (!g_edit->open_project_file(g_child_job.retimed.c_str(), false)) {
            child_fail(L"open_project_file returned false");
        }
        child_enter(ChildPhase::kWaitLoad);
        return;

    case ChildPhase::kWaitLoad:
        if (g_load_seq.load() != g_child_load_seq_before) {
            std::wstring loaded;
            {
                std::lock_guard<std::mutex> lock(g_path_mutex);
                loaded = g_loaded_path;
            }
            if (_wcsicmp(loaded.c_str(), g_child_job.retimed.c_str()) != 0) {
                child_fail(L"the loaded project differs from the retimed copy: " + loaded);
            }
            child_enter(ChildPhase::kStart);
        } else if (waited > g_timing.load_timeout_ms) {
            child_fail(L"the project load callback was not observed");
        }
        return;

    case ChildPhase::kStart: {
        wchar_t summary[512] = {};
        freefps_get_encoder_summary(summary, sizeof(summary) / sizeof(summary[0]), g_child_job.output.c_str());
        g_child_encoder = summary;
        log(false, L"export process encoder: " + g_child_encoder);
        g_save_seen = false;
        GetSystemTimeAsFileTime(&g_child_export_since);
        g_freefps_menu_export_active = true;  // the project is already re-timed: the built-in FreeRenderFPS stays off
        if (!g_edit->output_file(g_child_job.output.c_str(), g_child_job.plugin.c_str(), nullptr, nullptr)) {
            child_fail(L"output_file returned false");
        }
        child_enter(ChildPhase::kWaitExport);
        return;
    }

    case ChildPhase::kWaitExport:
        if (state == kStateSave) {
            g_save_seen = true;
        }
        if (g_save_seen && state == kStateEdit) {
            std::wstring why;
            long long bytes = -1;
            if (output_written(g_child_job.output, g_child_export_since, bytes, why)) {
                child_finish(true, std::wstring(), bytes);
            }
            child_fail(why);
        } else if (!g_save_seen && waited > g_timing.export_start_timeout_ms) {
            child_fail(L"the export start (CHANGE_EDIT_STATE) was not observed");
        } else if (waited > g_timing.export_timeout_ms) {
            child_fail(L"the export timed out");
        }
        return;
    }
}

void CALLBACK on_child_timer(HWND, UINT, UINT_PTR, DWORD) {
    child_tick();
}

// Called by the project load callback; only the first call starts the timer.
void child_start_timer_once() {
    if (!g_child_job.active || g_child_timer_started.exchange(true)) {
        return;
    }
    child_enter(ChildPhase::kWaitReady);
    // With a null window the timer id passed in is ignored; the id is never needed, as the process ends itself.
    if (SetTimer(nullptr, 0, g_timing.tick_ms, on_child_timer) == 0) {
        child_fail(L"cannot start the export timer");
    }
    log(false, L"export process: timer started");
}

DWORD WINAPI child_watchdog(LPVOID) {
    const ULONGLONG begin = GetTickCount64();
    for (;;) {
        Sleep(500);
        const ULONGLONG waited = GetTickCount64() - begin;
        if (!g_child_ticked.load() && waited > g_timing.child_start_timeout_ms) {
            child_fail(g_child_timer_started.load()
                           ? L"the export timer never ran (the project load callback is not on the message loop thread)"
                           : L"the host never called the project load callback, so the export could not start");
        }
        if (waited > g_child_job.timeout_ms + g_timing.child_start_timeout_ms) {
            child_fail(L"the export process timed out");
        }
    }
}

// Reads FREE_FPS_EXPORT_JOB and job.ini. Runs first in RegisterPlugin, because x264guiEx.cpp reads
// g_freefps_child_process while it prepares the output plugin. A variable that does not name an existing file is
// ignored (an ordinary start).
void detect_child_job() {
    std::vector<wchar_t> value(32768, L'\0');
    const DWORD length = GetEnvironmentVariableW(kJobEnv, value.data(), static_cast<DWORD>(value.size()));
    if (length == 0 || length >= value.size()) {
        return;
    }
    const std::wstring job_path(value.data(), length);
    if (!file_exists(job_path)) {
        return;
    }
    const std::size_t slash = job_path.find_last_of(L'\\');
    ChildJob job;
    job.active = true;
    job.job_path = job_path;
    job.result_path = (slash == std::wstring::npos ? std::wstring() : job_path.substr(0, slash + 1)) + kResultName;
    job.retimed = ini_get(job_path, kJobSection, L"retimed");
    job.output = ini_get(job_path, kJobSection, L"output");
    job.plugin = ini_get(job_path, kJobSection, L"plugin");
    const UINT timeout = GetPrivateProfileIntW(kJobSection, L"timeout_ms", 0, job_path.c_str());
    if (timeout > 0) {
        job.timeout_ms = timeout;
    }
    g_child_job = job;
    g_freefps_child_process = true;
}

// Checks the job and starts the watchdog. Ends the process with a result on a bad job.
void start_child_watchdog() {
    if (!g_child_job.active) {
        return;
    }
    if (g_child_job.plugin.empty()) {
        g_child_job.plugin = g_output_name;
    }
    if (g_child_job.retimed.empty() || g_child_job.output.empty() || !file_exists(g_child_job.retimed)) {
        child_fail(L"the job file is incomplete or the retimed project is missing: " + g_child_job.job_path);
    }
    HANDLE thread = CreateThread(nullptr, 0, child_watchdog, nullptr, 0, nullptr);
    if (thread == nullptr) {
        child_fail(L"cannot start the watchdog thread");
    }
    CloseHandle(thread);
    log(false, L"export process: job " + g_child_job.job_path);
}

// ---------------------------------------------------------------------------
// Dialog (built in memory, no resource file)
// ---------------------------------------------------------------------------

struct DialogValues {
    std::wstring rate;
    std::wstring output;
    bool precise = true;
    free_fps::SubsceneRatePolicy policy = free_fps::SubsceneRatePolicy::Auto;
    bool separate = false;           // export in a separate process (check box)
    bool separate_detected = false;  // the saved project uses SceneMotionBlur_K: the check box starts on, with a hint
    DebugConfig debug;
    // The scene plan reads the saved project. It is read once when the dialog opens: the path and the bytes, or no
    // path when the project has no file yet.
    std::wstring project_path;
    std::string project_bytes;
    bool project_read = false;
};

class TemplateBuilder {
public:
    void align() {
        while (bytes_.size() % 4 != 0) {
            bytes_.push_back(0);
        }
    }
    void word(WORD value) {
        bytes_.push_back(static_cast<unsigned char>(value & 0xFF));
        bytes_.push_back(static_cast<unsigned char>(value >> 8));
    }
    void dword(DWORD value) {
        word(static_cast<WORD>(value & 0xFFFF));
        word(static_cast<WORD>(value >> 16));
    }
    void text(const wchar_t* s) {
        while (*s != L'\0') {
            word(static_cast<WORD>(*s++));
        }
        word(0);
    }
    const void* data() const {
        return bytes_.data();
    }

private:
    std::vector<unsigned char> bytes_;
};

void add_item(TemplateBuilder& t, DWORD style, short x, short y, short cx, short cy, WORD id, WORD cls,
              const wchar_t* title) {
    t.align();
    t.dword(style);
    t.dword(0);  // extended style
    t.word(static_cast<WORD>(x));
    t.word(static_cast<WORD>(y));
    t.word(static_cast<WORD>(cx));
    t.word(static_cast<WORD>(cy));
    t.word(id);
    t.word(0xFFFF);  // class given by atom
    t.word(cls);     // 0x0080 button, 0x0081 edit, 0x0082 static
    t.text(title);
    t.word(0);  // no creation data
}

std::wstring get_text(HWND dlg, int id) {
    HWND item = GetDlgItem(dlg, id);
    const int length = GetWindowTextLengthW(item);
    std::vector<wchar_t> buffer(static_cast<std::size_t>(length) + 1, L'\0');
    GetWindowTextW(item, buffer.data(), static_cast<int>(buffer.size()));
    return std::wstring(buffer.data());
}

std::wstring trim(const std::wstring& s) {
    const std::size_t first = s.find_first_not_of(L" \t");
    if (first == std::wstring::npos) {
        return std::wstring();
    }
    return s.substr(first, s.find_last_not_of(L" \t") - first + 1);
}

void dialog_error(HWND dlg, const std::wstring& text) {
    MessageBoxW(dlg, text.c_str(), kTitle, MB_OK | MB_ICONERROR);
}

// Localized dialog text from the x264guiEx language file, or the English fallback when the key has no text.
const wchar_t* mes(AuoMes id, const wchar_t* fallback) {
    const wchar_t* text = g_auo_mes.get(id);
    return (text != nullptr && text[0] != L'\0') ? text : fallback;
}

// The default output container without the dot, e.g. "mp4" (x264guiEx default_output_ext).
std::wstring container_name() {
    wchar_t name[32] = {};
    freefps_get_container_name(name, sizeof(name) / sizeof(name[0]));
    return name;
}

// Width of a control's text in dialog units, measured with the control's own font, plus extra units.
int text_width_dlu(HWND dlg, int id, int extra) {
    HWND ctl = GetDlgItem(dlg, id);
    const std::wstring text = get_text(dlg, id);
    HDC dc = GetDC(ctl);
    HFONT font = reinterpret_cast<HFONT>(SendMessageW(ctl, WM_GETFONT, 0, 0));
    HGDIOBJ old_font = SelectObject(dc, font != nullptr ? font : GetStockObject(DEFAULT_GUI_FONT));
    RECT measured{};
    DrawTextW(dc, text.c_str(), -1, &measured, DT_CALCRECT | DT_SINGLELINE | DT_NOPREFIX);
    SelectObject(dc, old_font);
    ReleaseDC(ctl, dc);
    RECT unit{0, 0, 4, 8};  // 4 x 8 dialog units in pixels
    MapDialogRect(dlg, &unit);
    const double pixels_per_unit = unit.right / 4.0;
    return static_cast<int>(std::ceil((measured.right - measured.left) / pixels_per_unit)) + extra;
}

void place_item(HWND dlg, int id, int x, int y, int w, int h) {
    RECT rc{x, y, x + w, y + h};
    MapDialogRect(dlg, &rc);
    SetWindowPos(GetDlgItem(dlg, id), nullptr, rc.left, rc.top, rc.right - rc.left, rc.bottom - rc.top,
                 SWP_NOZORDER);
}

bool is_shown(HWND dlg, int id) {
    return (GetWindowLongPtrW(GetDlgItem(dlg, id), GWL_STYLE) & WS_VISIBLE) != 0;
}

// Height in dialog units of a static text wrapped to width_dlu, measured with the control's own font. The text is
// wrapped at spaces, as the static control wraps it.
int wrapped_height_dlu(HWND dlg, int id, int width_dlu) {
    HWND ctl = GetDlgItem(dlg, id);
    const std::wstring text = get_text(dlg, id);
    RECT unit{0, 0, 4, 8};  // 4 x 8 dialog units in pixels
    MapDialogRect(dlg, &unit);
    HDC dc = GetDC(ctl);
    HFONT font = reinterpret_cast<HFONT>(SendMessageW(ctl, WM_GETFONT, 0, 0));
    HGDIOBJ old_font = SelectObject(dc, font != nullptr ? font : GetStockObject(DEFAULT_GUI_FONT));
    RECT measured{0, 0, unit.right * width_dlu / 4, 0};
    DrawTextW(dc, text.c_str(), -1, &measured, DT_CALCRECT | DT_WORDBREAK | DT_NOPREFIX);
    SelectObject(dc, old_font);
    ReleaseDC(ctl, dc);
    const double pixels_per_unit_y = unit.bottom / 8.0;
    return static_cast<int>(std::ceil((measured.bottom - measured.top) / pixels_per_unit_y)) + 1;
}

// Places every control in dialog units, in pixels of the dialog's own font, and sizes the dialog. The content width is
// kBaseContentWidth, or wider when a label, check box or the summary needs more, so no text is cut off in any language.
// The rows follow each other from the top. A hidden row (the scene plan with one scene) takes no space.
void layout_dialog(HWND dlg) {
    const bool precise_shown = is_shown(dlg, kIdPrecise);
    int content = kBaseContentWidth;
    for (const int need : {text_width_dlu(dlg, kIdRateLabel, 0), text_width_dlu(dlg, kIdOutputLabel, 0),
                           precise_shown ? text_width_dlu(dlg, kIdPrecise, kCheckPadding) : 0,
                           text_width_dlu(dlg, kIdSubsceneLabel, 0), text_width_dlu(dlg, kIdSummary, 0),
                           text_width_dlu(dlg, kIdSeparate, kCheckPadding)}) {
        content = (std::max)(content, need);
    }
    const int browse_w = (std::max)(44, text_width_dlu(dlg, kIdBrowse, kButtonPadding));
    const int enc_w = (std::max)(90, text_width_dlu(dlg, kIdEncSettings, kButtonPadding));
    const int ok_w = (std::max)(50, text_width_dlu(dlg, IDOK, kButtonPadding));
    const int cancel_w = (std::max)(50, text_width_dlu(dlg, IDCANCEL, kButtonPadding));

    const int right = kMargin + content;
    place_item(dlg, kIdRateLabel, kMargin, 7, content, 10);
    place_item(dlg, kIdRate, kMargin, 18, content, 14);
    place_item(dlg, kIdOutputLabel, kMargin, 38, content, 10);
    place_item(dlg, kIdOutput, kMargin, 48, content - browse_w - 4, 14);
    place_item(dlg, kIdBrowse, right - browse_w, 48, browse_w, 14);
    int row_y = 66;
    if (precise_shown) {
        place_item(dlg, kIdPrecise, kMargin, row_y, content, 12);
        row_y += 18;
    }
    place_item(dlg, kIdSubsceneLabel, kMargin, row_y, content, 10);
    place_item(dlg, kIdSubsceneCombo, kMargin, row_y + 11, content, kSubsceneComboHeight);
    row_y += 11 + kSubsceneComboHeight + 6;
    if (is_shown(dlg, kIdScenePlan)) {
        const int plan_h = wrapped_height_dlu(dlg, kIdScenePlan, content);
        place_item(dlg, kIdScenePlan, kMargin, row_y, content, plan_h);
        row_y += plan_h + 6;
    }
    place_item(dlg, kIdSeparate, kMargin, row_y, content, 12);
    row_y += 16;
    if (is_shown(dlg, kIdSeparateHint)) {
        const int hint_h = wrapped_height_dlu(dlg, kIdSeparateHint, content);
        place_item(dlg, kIdSeparateHint, kMargin, row_y, content, hint_h);
        row_y += hint_h + 6;
    }
    place_item(dlg, kIdSummary, kMargin, row_y, content, 10);
    place_item(dlg, kIdEncSettings, kMargin, row_y + 16, enc_w, 14);
    const int buttons_y = row_y + 36;
    place_item(dlg, IDCANCEL, right - cancel_w, buttons_y, cancel_w, 14);
    place_item(dlg, IDOK, right - cancel_w - 4 - ok_w, buttons_y, ok_w, 14);

    const int dialog_h = buttons_y + 14 + 6;  // 6 dialog units below the buttons
    RECT frame{0, 0, 2 * kMargin + content, dialog_h};
    MapDialogRect(dlg, &frame);  // client size in pixels
    AdjustWindowRectEx(&frame, static_cast<DWORD>(GetWindowLongPtrW(dlg, GWL_STYLE)), FALSE,
                       static_cast<DWORD>(GetWindowLongPtrW(dlg, GWL_EXSTYLE)));
    const int width = frame.right - frame.left;
    const int height = frame.bottom - frame.top;
    RECT current{};
    GetWindowRect(dlg, &current);
    if (width != current.right - current.left || height != current.bottom - current.top) {
        // Keep the dialog centred on its owner, as DS_CENTER did for the first size.
        RECT anchor{};
        HWND owner = GetWindow(dlg, GW_OWNER);
        if (owner == nullptr || !GetWindowRect(owner, &anchor)) {
            MONITORINFO info{sizeof(info)};
            GetMonitorInfoW(MonitorFromWindow(dlg, MONITOR_DEFAULTTONEAREST), &info);
            anchor = info.rcWork;
        }
        const int x = anchor.left + ((anchor.right - anchor.left) - width) / 2;
        const int y = anchor.top + ((anchor.bottom - anchor.top) - height) / 2;
        SetWindowPos(dlg, nullptr, x, y, width, height, SWP_NOZORDER);
    }
}

// Shows the encoder summary (the output settings, with the container of the current output file name) and re-fits
// the dialog to it.
void refresh_summary(HWND dlg) {
    const std::wstring output = trim(get_text(dlg, kIdOutput));
    wchar_t summary[512] = {};
    freefps_get_encoder_summary(summary, sizeof(summary) / sizeof(summary[0]),
                                output.empty() ? nullptr : output.c_str());
    std::wstring prefix = mes(AuoFreeFpsExportEncSummary, L"Encoder: ");
    if (!prefix.empty() && prefix.back() != L' ') {
        prefix += L' ';  // the language file value may have no trailing space
    }
    const std::wstring text = prefix + summary;
    SetDlgItemTextW(dlg, kIdSummary, text.c_str());
    layout_dialog(dlg);
}

// The policy the sub-scene rate combo box shows: 0 auto, 1 follow, 2 keep (the order of the items in WM_INITDIALOG).
free_fps::SubsceneRatePolicy selected_policy(HWND dlg) {
    switch (static_cast<int>(SendMessageW(GetDlgItem(dlg, kIdSubsceneCombo), CB_GETCURSEL, 0, 0))) {
    case 1:
        return free_fps::SubsceneRatePolicy::Follow;
    case 2:
        return free_fps::SubsceneRatePolicy::Keep;
    default:
        return free_fps::SubsceneRatePolicy::Auto;
    }
}

// A rate as the plan shows it: "60/1" is "60", a fraction such as "60000/1001" stays as it is.
std::wstring display_rate(const std::string& rate) {
    std::string shown = rate;
    const std::string whole_suffix = "/1";
    if (shown.size() > whole_suffix.size() &&
        shown.compare(shown.size() - whole_suffix.size(), whole_suffix.size(), whole_suffix) == 0) {
        shown.erase(shown.size() - whole_suffix.size());
    }
    return to_wide(shown);
}

// One line per scene, e.g. "Main 30->60 (follow) | Sub-scene1 60->60 (follow) | Sub-scene2 15->15 (keep)". A scene's
// name is shown when it has one; otherwise the exported scene is the main one and the others are numbered.
std::wstring scene_plan_text(const RetimeResult& result) {
    std::wstring text;
    for (const free_fps::ScenePlan& scene : result.scenes) {
        std::wstring label = to_wide(scene.name);
        if (label.empty()) {
            if (scene.exported) {
                label = mes(AuoFreeFpsExportScenePlanMain, L"Main");
            } else {
                label = mes(AuoFreeFpsExportScenePlanSub, L"Sub-scene") + std::to_wstring(scene.scene);
            }
        }
        const std::wstring word = scene.follow ? mes(AuoFreeFpsExportScenePlanFollow, L"follow")
                                               : mes(AuoFreeFpsExportScenePlanKeep, L"keep");
        if (!text.empty()) {
            text += L" | ";
        }
        text += label + L" " + display_rate(scene.source_rate) + L"\u2192" + display_rate(scene.target_rate) +
                L" \uFF08" + word + L"\uFF09";
    }
    return text;
}

// Fills the sub-scene rate combo box in the order selected_policy() reads it, and selects the first item (auto).
void init_policy_combo(HWND dlg) {
    HWND policy = GetDlgItem(dlg, kIdSubsceneCombo);
    const std::wstring items[] = {
        mes(AuoFreeFpsExportSubsceneAuto, L"Auto (recommended)"),
        mes(AuoFreeFpsExportSubsceneFollow, L"Follow the target rate for all scenes"),
        mes(AuoFreeFpsExportSubsceneKeep, L"Keep the original rate for all scenes"),
    };
    for (const std::wstring& item : items) {
        SendMessageW(policy, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(item.c_str()));
    }
    SendMessageW(policy, CB_SETCURSEL, 0, 0);
}

// Re-runs the plan for the target rate, sub-scene policy and precise setting in the dialog. retime() never writes, so
// the output path is only the placeholder it needs. Hidden when the target rate is not valid yet, and for a single
// scene (the policy has nothing to decide there). Shown as the message when the project is unsaved or refused.
void refresh_scene_plan(HWND dlg) {
    DialogValues* values = reinterpret_cast<DialogValues*>(GetWindowLongPtrW(dlg, DWLP_USER));
    std::wstring text;
    bool show = false;
    Rational target;
    std::string error;
    const std::wstring rate = trim(get_text(dlg, kIdRate));
    if (values != nullptr && free_fps::parse_target_rate(free_fps::wide_to_utf8(rate), target, error)) {
        if (values->project_path.empty()) {
            text = mes(AuoFreeFpsExportScenePlanUnsaved, L"Save the project to preview");
            show = true;
        } else if (values->project_read) {
            const bool precise = IsDlgButtonChecked(dlg, kIdPrecise) == BST_CHECKED;
            const RetimeResult result =
                free_fps::retime(values->project_bytes, target, values->project_path, precise,
                                 static_cast<std::size_t>(values->debug.extra_decimals), selected_policy(dlg));
            if (!result.ok) {
                text = to_wide(free_fps::refusal_message(result));
                show = true;
            } else if (result.scenes.size() > 1) {
                text = mes(AuoFreeFpsExportScenePlanPrefix, L"Preview from the saved project: ") +
                       scene_plan_text(result);
                show = true;
            }
        }
    }
    ShowWindow(GetDlgItem(dlg, kIdScenePlan), show ? SW_SHOW : SW_HIDE);
    SetDlgItemTextW(dlg, kIdScenePlan, text.c_str());
    layout_dialog(dlg);
}

INT_PTR CALLBACK dialog_proc(HWND dlg, UINT message, WPARAM wparam, LPARAM lparam) {
    switch (message) {
    case WM_INITDIALOG: {
        SetWindowLongPtrW(dlg, DWLP_USER, lparam);
        DialogValues* init = reinterpret_cast<DialogValues*>(lparam);
        std::wstring title = mes(AuoFreeFpsExportTitle, kTitle);
        if (init->debug.enabled) {
            title += L" [Debug]";
        }
        SetWindowTextW(dlg, title.c_str());
        SetDlgItemTextW(dlg, kIdRateLabel,
                        mes(AuoFreeFpsExportTargetFps, L"Target FPS (exact: 66, 60000/1001; decimals are refused)"));
        SetDlgItemTextW(dlg, kIdOutputLabel, mes(AuoFreeFpsExportOutputFile, L"Output file"));
        SetDlgItemTextW(dlg, kIdBrowse, mes(AuoFreeFpsExportBrowse, L"Browse..."));
        SetDlgItemTextW(dlg, kIdPrecise,
                        mes(AuoFreeFpsExportPrecise, L"Precise motion (exact speed for linear motion)"));
        SetDlgItemTextW(dlg, kIdEncSettings, mes(AuoFreeFpsExportEncSettings, L"Encoder settings..."));
        SetDlgItemTextW(dlg, IDOK, mes(AuoFreeFpsExportOK, L"OK"));
        SetDlgItemTextW(dlg, IDCANCEL, mes(AuoFreeFpsExportCancel, L"Cancel"));
        SetDlgItemTextW(dlg, kIdSubsceneLabel, mes(AuoFreeFpsExportSubsceneRate, L"Sub-scene frame rate:"));
        SetDlgItemTextW(dlg, kIdSeparate,
                        mes(AuoFreeFpsExportSeparateProcess,
                            L"Export in a separate process (avoids a crash when switching projects)"));
        SetDlgItemTextW(dlg, kIdSeparateHint,
                        mes(AuoFreeFpsExportSeparateProcessHint,
                            L"This project uses SceneMotionBlur_K, which crashes AviUtl2 when the project is "
                            L"switched. Exporting in a separate process by default."));
        CheckDlgButton(dlg, kIdSeparate, init->separate ? BST_CHECKED : BST_UNCHECKED);
        ShowWindow(GetDlgItem(dlg, kIdSeparateHint), init->separate_detected ? SW_SHOW : SW_HIDE);
        init_policy_combo(dlg);
        CheckDlgButton(dlg, kIdPrecise, init->precise ? BST_CHECKED : BST_UNCHECKED);
        if (!init->debug.enabled) {
            // Normal mode: precise motion is forced on, and the check box is hidden. Hidden rows take no space.
            ShowWindow(GetDlgItem(dlg, kIdPrecise), SW_HIDE);
        }
        refresh_summary(dlg);
        refresh_scene_plan(dlg);
        return TRUE;
    }

    case WM_COMMAND:
        if (LOWORD(wparam) == kIdBrowse) {
            std::vector<wchar_t> file(4096, L'\0');
            const std::wstring current = get_text(dlg, kIdOutput);
            wcsncpy_s(file.data(), file.size(), current.c_str(), _TRUNCATE);
            // The filter and the default extension follow the default output container.
            const std::wstring name = container_name();
            const std::wstring pattern = L"*." + name;
            std::wstring filter = name + L" file (" + pattern + L")";
            filter.push_back(L'\0');
            filter += pattern;
            filter.push_back(L'\0');
            filter += L"All files (*.*)";
            filter.push_back(L'\0');
            filter += L"*.*";
            filter.push_back(L'\0');
            filter.push_back(L'\0');
            OPENFILENAMEW ofn{};
            ofn.lStructSize = sizeof(ofn);
            ofn.hwndOwner = dlg;
            ofn.lpstrFilter = filter.c_str();
            ofn.lpstrFile = file.data();
            ofn.nMaxFile = static_cast<DWORD>(file.size());
            ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
            ofn.lpstrDefExt = name.c_str();
            if (GetSaveFileNameW(&ofn)) {
                SetDlgItemTextW(dlg, kIdOutput, file.data());
                refresh_summary(dlg);
            }
            return TRUE;
        }
        if (LOWORD(wparam) == kIdOutput && HIWORD(wparam) == EN_CHANGE) {
            refresh_summary(dlg);  // the container shown follows the output file name
            return TRUE;
        }
        if ((LOWORD(wparam) == kIdRate && HIWORD(wparam) == EN_CHANGE) ||
            (LOWORD(wparam) == kIdSubsceneCombo && HIWORD(wparam) == CBN_SELCHANGE) ||
            (LOWORD(wparam) == kIdPrecise && HIWORD(wparam) == BN_CLICKED)) {
            refresh_scene_plan(dlg);
            return TRUE;
        }
        if (LOWORD(wparam) == kIdEncSettings) {
            if (!freefps_open_encoder_settings()) {
                MessageBoxW(dlg, L"The encoder settings are not available.", kTitle, MB_OK | MB_ICONERROR);
            }
            refresh_summary(dlg);
            return TRUE;
        }
        if (LOWORD(wparam) == IDCANCEL) {
            EndDialog(dlg, IDCANCEL);
            return TRUE;
        }
        if (LOWORD(wparam) == IDOK) {
            DialogValues* values = reinterpret_cast<DialogValues*>(GetWindowLongPtrW(dlg, DWLP_USER));
            const std::wstring rate = trim(get_text(dlg, kIdRate));
            std::wstring output = trim(get_text(dlg, kIdOutput));
            Rational parsed;
            std::string error;
            if (!free_fps::parse_target_rate(free_fps::wide_to_utf8(rate), parsed, error)) {
                dialog_error(dlg, L"Target FPS: " + to_wide(error));
                SetFocus(GetDlgItem(dlg, kIdRate));
                return TRUE;
            }
            if (output.empty()) {
                dialog_error(dlg, L"Choose an output file.");
                SetFocus(GetDlgItem(dlg, kIdOutput));
                return TRUE;
            }
            const std::size_t slash = output.find_last_of(L'\\');
            const std::wstring folder = slash == std::wstring::npos ? std::wstring(L".") : output.substr(0, slash);
            const DWORD attributes = GetFileAttributesW(folder.c_str());
            if (attributes == INVALID_FILE_ATTRIBUTES || (attributes & FILE_ATTRIBUTE_DIRECTORY) == 0) {
                dialog_error(dlg, L"The output folder does not exist:\n" + folder);
                SetFocus(GetDlgItem(dlg, kIdOutput));
                return TRUE;
            }
            if (!freefps_has_known_container_ext(output.c_str())) {
                // The encoder appends its default container extension to a name it does not recognise. Do the same
                // here, so the name in the box is the file that is written and the output check matches it.
                if (output.back() == L'\\') {
                    dialog_error(dlg, L"Choose an output file.");
                    SetFocus(GetDlgItem(dlg, kIdOutput));
                    return TRUE;
                }
                output += L"." + container_name();
                SetDlgItemTextW(dlg, kIdOutput, output.c_str());
                refresh_summary(dlg);
            }
            values->rate = rate;
            values->output = output;
            values->precise = values->debug.enabled ? IsDlgButtonChecked(dlg, kIdPrecise) == BST_CHECKED : true;
            values->policy = selected_policy(dlg);
            values->separate = IsDlgButtonChecked(dlg, kIdSeparate) == BST_CHECKED;
            EndDialog(dlg, IDOK);
            return TRUE;
        }
        return FALSE;
    }
    return FALSE;
}

// Shows the modal dialog. Returns true with values filled when the user pressed OK.
bool ask_for_request(HWND owner, DialogValues& values) {
    TemplateBuilder t;
    // DLGTEMPLATE header: style, extended style, item count, x, y, width, height (dialog units).
    const DWORD style = DS_MODALFRAME | DS_CENTER | DS_SETFONT | WS_POPUP | WS_CAPTION | WS_SYSMENU;
    t.dword(style);
    t.dword(0);
    t.word(15);  // item count
    t.word(0);
    t.word(0);
    t.word(static_cast<WORD>(2 * kMargin + kBaseContentWidth));
    t.word(static_cast<WORD>(kDialogHeight));
    t.word(0);  // no menu
    t.word(0);  // default class
    t.text(kTitle);
    t.word(9);  // font point size
    t.text(L"MS Shell Dlg");

    // Texts are empty here and set in WM_INITDIALOG; the positions are placed again by layout_dialog.
    const DWORD static_style = WS_CHILD | WS_VISIBLE | SS_LEFT;
    const DWORD edit_style = WS_CHILD | WS_VISIBLE | WS_BORDER | WS_TABSTOP | ES_AUTOHSCROLL;
    const DWORD button_style = WS_CHILD | WS_VISIBLE | WS_TABSTOP;
    const DWORD combo_style = WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_VSCROLL | CBS_DROPDOWNLIST;
    add_item(t, static_style, 7, 7, 248, 10, kIdRateLabel, 0x0082, L"");
    add_item(t, edit_style, 7, 18, 248, 14, kIdRate, 0x0081, L"");
    add_item(t, static_style, 7, 38, 248, 10, kIdOutputLabel, 0x0082, L"");
    add_item(t, edit_style, 7, 48, 200, 14, kIdOutput, 0x0081, L"");
    add_item(t, button_style, 211, 48, 44, 14, kIdBrowse, 0x0080, L"");
    add_item(t, button_style | BS_AUTOCHECKBOX, 7, 66, 248, 12, kIdPrecise, 0x0080, L"");
    add_item(t, static_style, 7, 84, 248, 10, kIdSubsceneLabel, 0x0082, L"");
    add_item(t, combo_style, 7, 95, 248, kSubsceneComboHeight, kIdSubsceneCombo, 0x0085, L"");
    add_item(t, static_style, 7, 139, 248, 10, kIdScenePlan, 0x0082, L"");
    add_item(t, button_style | BS_AUTOCHECKBOX, 7, 139, 248, 12, kIdSeparate, 0x0080, L"");
    add_item(t, static_style, 7, 139, 248, 10, kIdSeparateHint, 0x0082, L"");
    add_item(t, static_style, 7, 84, 248, 10, kIdSummary, 0x0082, L"");
    add_item(t, button_style, 7, 100, 90, 14, kIdEncSettings, 0x0080, L"");
    add_item(t, button_style | BS_DEFPUSHBUTTON, 146, 120, 50, 14, IDOK, 0x0080, L"");
    add_item(t, button_style, 200, 120, 50, 14, IDCANCEL, 0x0080, L"");

    const INT_PTR result = DialogBoxIndirectParamW(g_instance, reinterpret_cast<LPCDLGTEMPLATEW>(t.data()), owner,
                                                   dialog_proc, reinterpret_cast<LPARAM>(&values));
    return result == IDOK;
}

void on_menu(void*) {
    if (g_run) {
        g_notify(false, L"An export is already running.");
        return;
    }
    if (g_edit == nullptr) {
        return;
    }
    if (g_output_name.empty()) {
        g_notify(false, L"The output plugin is not registered.");
        return;
    }
    DialogValues values;
    values.debug = load_debug_config();
    values.precise = values.debug.precise;
    values.project_path = current_project_path();
    if (!values.project_path.empty()) {
        values.project_read = read_file_bytes(values.project_path, values.project_bytes);
    }
    values.separate_detected = values.project_read && uses_scene_motion_blur(values.project_bytes);
    values.separate = values.separate_detected;
    if (!ask_for_request(g_edit->get_host_app_window(), values)) {
        return;
    }
    Rational target;
    std::string error;
    if (!free_fps::parse_target_rate(free_fps::wide_to_utf8(values.rate), target, error)) {
        g_notify(false, L"Target FPS: " + to_wide(error));
        return;
    }
    ExportRequest request;
    request.target = target;
    request.precise = values.precise;
    request.policy = values.policy;
    request.extra_decimals = static_cast<std::size_t>(values.debug.extra_decimals);
    request.keep_outputs = values.debug.keep_temp;
    request.separate_process = values.separate;
    request.output = values.output;
    request.plugin = g_output_name;
    begin_export(request);
}

// Project load callback: the path of the project that was just loaded, and the current project path.
void on_project_load(PROJECT_FILE* project) {
    std::wstring path;
    if (project != nullptr && project->get_project_file_path != nullptr) {
        const LPCWSTR current = project->get_project_file_path();
        if (current != nullptr) {
            path = current;
        }
    }
    {
        std::lock_guard<std::mutex> lock(g_path_mutex);
        g_loaded_path = path;
        g_project_path = path;
    }
    g_load_seq.fetch_add(1);
    child_start_timer_once();  // second host only: the first load callback starts the export
}

// Project save callback: the path the project is saved to (covers Save As).
void on_project_save(PROJECT_FILE* project) {
    if (project == nullptr || project->get_project_file_path == nullptr) {
        return;
    }
    const LPCWSTR current = project->get_project_file_path();
    std::lock_guard<std::mutex> lock(g_path_mutex);
    g_project_path = current != nullptr ? current : L"";
}

void on_edit_state_event(void*) {
    if (g_edit != nullptr && g_edit->get_edit_state() == kStateSave) {
        g_save_seen = true;
    }
}

}  // namespace

void freefps_export_set_logger(LOG_HANDLE* logger) {
    g_logger = logger;
}

extern "C" {

__declspec(dllexport) COMMON_PLUGIN_TABLE* GetCommonPluginTable() {
    static COMMON_PLUGIN_TABLE table = {
        AUO_FULL_NAME_W,
        AUO_VERSION_INFO_W,
    };
    return &table;
}

// Registers this DLL's output plugin table (prepared the same way GetOutputPluginTable prepares it for AviUtl 1.x),
// then creates the edit handle and registers the project load and save handlers, the CHANGE_EDIT_STATE listener and
// the Edit menu.
__declspec(dllexport) void RegisterPlugin(HOST_APP_TABLE* host) {
    if (host == nullptr) {
        return;
    }
    detect_child_job();  // before prepare_output_plugin_table: x264guiEx.cpp reads g_freefps_child_process
    // Instance handle for the in-memory dialog template: this DLL, found from one of its own exports.
    HMODULE self = nullptr;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                       reinterpret_cast<LPCWSTR>(&RegisterPlugin), &self);
    g_instance = self;

    OUTPUT_PLUGIN_TABLE* output = prepare_output_plugin_table();
    host->register_output_plugin(output);
    g_output_name = output->name;

    g_edit = host->create_edit_handle();
    host->register_project_load_handler(on_project_load);
    host->register_project_save_handler(on_project_save);
    host->register_event_listener(EVENT_TYPE::CHANGE_EDIT_STATE, nullptr, on_edit_state_event);
    host->register_edit_menu_param(kMenuName, nullptr, on_menu);
    start_child_watchdog();  // second host only
}

__declspec(dllexport) void UninitializePlugin() {
    if (g_run) {
        KillTimer(nullptr, kTimerId);
        if (g_run->child != nullptr) {
            CloseHandle(g_run->child);  // the second host, if it still runs, finishes its export on its own
            g_run->child = nullptr;
        }
        g_run.reset();
    }
    g_freefps_menu_export_active = false;
}

}  // extern "C"
