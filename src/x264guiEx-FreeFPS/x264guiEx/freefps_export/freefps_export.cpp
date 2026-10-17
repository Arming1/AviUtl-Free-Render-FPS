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
//   2. Dialog: target FPS (exact rational: 66, 60000/1001; decimals refused) and output file.
//   3. Confirm: the SDK has no unsaved-changes flag, so the user is always asked whether to save the project to its
//      own path first. No (or closing the prompt) aborts with no side effects.
//   4. save_project_file(original) once confirmed, then copy the saved original to
//      %TEMP%\FreeFpsExport\<timestamp>\src.aup2 (a file copy, not a save to temp).
//   5. retime() writes retimed.aup2 and retimed.aup2.convert.json beside it, in the same temp folder.
//   6. open_project_file(retimed.aup2, false), then output_file(out, <own plugin name>, nullptr, nullptr).
//   7. Wait for CHANGE_EDIT_STATE (SAVE, then EDIT) and check the output file.
//   8. open_project_file(original, false) on every outcome once the user has confirmed. The original project is
//      the one reopened, never the temp copy.
//   9. Delete the temp folder. FREE_FPS_EXPORT_KEEP_TEMP=1 keeps it for debugging.
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

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "auo_version.h"
#include "logger2.h"
#include "output2.h"
#include "plugin2.h"
#include "retime.h"
#include "freefps_export.h"

#pragma comment(lib, "user32.lib")     // SetTimer, MessageBoxW, dialogs
#pragma comment(lib, "comdlg32.lib")   // GetSaveFileNameW

std::atomic<bool> g_freefps_menu_export_active{false};  // see freefps_export.h

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
};

Timing g_timing;

constexpr UINT_PTR kTimerId = 0x4646;
constexpr wchar_t kTitle[] = L"Free FPS Export";
constexpr wchar_t kMenuName[] = L"Free FPS Export...";
constexpr wchar_t kKeepTempEnv[] = L"FREE_FPS_EXPORT_KEEP_TEMP";
constexpr wchar_t kSrcName[] = L"src.aup2";
constexpr wchar_t kRetimedName[] = L"retimed.aup2";
constexpr wchar_t kSidecarName[] = L".convert.json";
// FAT volumes store write times with 2 s resolution. The export must have written the file at or after
// start minus this slack.
constexpr LONGLONG kWriteTimeSlack100ns = 20000000LL;

constexpr int kStateEdit = EDIT_HANDLE::EDIT_STATE_EDIT;
constexpr int kStateSave = EDIT_HANDLE::EDIT_STATE_SAVE;

// Dialog control IDs.
constexpr int kIdRate = 101;
constexpr int kIdOutput = 102;
constexpr int kIdBrowse = 103;

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

// Deletes only the files this plugin writes, then the folder. Files that cannot be deleted (for example a file the
// host still holds) are logged and left in place.
void remove_work_dir(const std::wstring& dir) {
    if (dir.empty()) {
        return;
    }
    for (const std::wstring name : {std::wstring(kSrcName), std::wstring(kRetimedName),
                                    std::wstring(kRetimedName) + kSidecarName}) {
        if (!DeleteFileW((dir + name).c_str()) && GetLastError() != ERROR_FILE_NOT_FOUND) {
            log(true, L"cannot delete temp file " + dir + name);
        }
    }
    if (!RemoveDirectoryW(dir.c_str())) {
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
    std::wstring output;
    std::wstring plugin;
    std::wstring original;  // the project file path, set by begin_export()
};

enum class Phase { kWaitReady, kSave, kOpen, kWaitLoad, kStart, kWaitExport, kRestore, kWaitRestoreLoad, kDone };

struct Run {
    ExportRequest request;
    std::wstring work_dir;
    std::wstring src_path;
    std::wstring retimed_path;
    std::wstring json_path;
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

// Records the first failure. The original project is reopened afterwards, on every outcome once confirmed.
void fail(Run& run, const std::wstring& reason) {
    if (run.failure.empty()) {
        run.failure = reason;
    }
    log(true, reason);
    enter(run, Phase::kRestore);
}

std::wstring rate_label(const Rational& r) {
    return std::to_wstring(r.num) + L"/" + std::to_wstring(r.den);
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
    } else {
        text = L"Export failed.\n" + run->failure;
    }
    if (!run->restore_note.empty()) {
        text += L"\n\n" + run->restore_note;
    }
    const bool keep = keep_temp_requested();
    if (keep) {
        text += L"\n\nTemp files kept (" + std::wstring(kKeepTempEnv) + L"=1): " + run->work_dir;
    } else {
        remove_work_dir(run->work_dir);
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
        const RetimeResult result = free_fps::retime(src_bytes, run.request.target, run.retimed_path);
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
        enter(run, Phase::kOpen);
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
// Dialog (built in memory, no resource file)
// ---------------------------------------------------------------------------

struct DialogValues {
    std::wstring rate;
    std::wstring output;
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

INT_PTR CALLBACK dialog_proc(HWND dlg, UINT message, WPARAM wparam, LPARAM lparam) {
    switch (message) {
    case WM_INITDIALOG:
        SetWindowLongPtrW(dlg, DWLP_USER, lparam);
        return TRUE;

    case WM_COMMAND:
        if (LOWORD(wparam) == kIdBrowse) {
            std::vector<wchar_t> file(4096, L'\0');
            const std::wstring current = get_text(dlg, kIdOutput);
            wcsncpy_s(file.data(), file.size(), current.c_str(), _TRUNCATE);
            static const wchar_t kFilter[] = L"MP4 video (*.mp4)\0*.mp4\0All files (*.*)\0*.*\0\0";
            OPENFILENAMEW ofn{};
            ofn.lStructSize = sizeof(ofn);
            ofn.hwndOwner = dlg;
            ofn.lpstrFilter = kFilter;
            ofn.lpstrFile = file.data();
            ofn.nMaxFile = static_cast<DWORD>(file.size());
            ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
            ofn.lpstrDefExt = L"mp4";
            if (GetSaveFileNameW(&ofn)) {
                SetDlgItemTextW(dlg, kIdOutput, file.data());
            }
            return TRUE;
        }
        if (LOWORD(wparam) == IDCANCEL) {
            EndDialog(dlg, IDCANCEL);
            return TRUE;
        }
        if (LOWORD(wparam) == IDOK) {
            DialogValues* values = reinterpret_cast<DialogValues*>(GetWindowLongPtrW(dlg, DWLP_USER));
            const std::wstring rate = trim(get_text(dlg, kIdRate));
            const std::wstring output = trim(get_text(dlg, kIdOutput));
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
            values->rate = rate;
            values->output = output;
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
    t.word(7);  // item count
    t.word(0);
    t.word(0);
    t.word(262);
    t.word(90);
    t.word(0);  // no menu
    t.word(0);  // default class
    t.text(kTitle);
    t.word(9);  // font point size
    t.text(L"MS Shell Dlg");

    const DWORD static_style = WS_CHILD | WS_VISIBLE | SS_LEFT;
    const DWORD edit_style = WS_CHILD | WS_VISIBLE | WS_BORDER | WS_TABSTOP | ES_AUTOHSCROLL;
    add_item(t, static_style, 7, 7, 248, 10, 0xFFFF, 0x0082, L"Target FPS (exact: 66, 60000/1001; decimals are refused)");
    add_item(t, edit_style, 7, 18, 248, 14, kIdRate, 0x0081, L"");
    add_item(t, static_style, 7, 38, 248, 10, 0xFFFF, 0x0082, L"Output file");
    add_item(t, edit_style, 7, 48, 200, 14, kIdOutput, 0x0081, L"");
    add_item(t, WS_CHILD | WS_VISIBLE | WS_TABSTOP, 211, 48, 44, 14, kIdBrowse, 0x0080, L"Browse...");
    add_item(t, WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON, 146, 68, 50, 14, IDOK, 0x0080, L"OK");
    add_item(t, WS_CHILD | WS_VISIBLE | WS_TABSTOP, 200, 68, 50, 14, IDCANCEL, 0x0080, L"Cancel");

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
}

__declspec(dllexport) void UninitializePlugin() {
    if (g_run) {
        KillTimer(nullptr, kTimerId);
        g_run.reset();
    }
    g_freefps_menu_export_active = false;
}

}  // extern "C"
