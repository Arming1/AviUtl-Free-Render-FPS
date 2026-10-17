// Host-free test for experiments/free_fps_export/free_fps_export.cpp.
//
// The plugin source is included directly so that the state machine and timing limits can be reached from a fake
// host (HOST_APP_TABLE, EDIT_HANDLE, PROJECT_FILE). No AviUtl window and no message box is created: the result hook
// (g_notify) and the save prompt hook (g_confirm_save) are replaced. The test only pumps this thread's message queue
// so the plugin's thread timer fires. Files are written only under %TEMP%\FreeFpsExportTest\<pid>\.
//
// Build and run: experiments\free_fps_export\build_and_run.ps1

#include "../free_fps_export.cpp"

#include <chrono>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

namespace {

int g_failures = 0;

void check(bool condition, const std::string& name, const std::string& detail = std::string()) {
    if (condition) {
        std::printf("PASS %s\n", name.c_str());
    } else {
        std::printf("FAIL %s%s%s\n", name.c_str(), detail.empty() ? "" : " : ", detail.c_str());
        ++g_failures;
    }
}

std::string narrow(const std::wstring& text) {
    return free_fps::wide_to_utf8(text);
}

bool contains(const std::wstring& text, const std::wstring& needle) {
    return text.find(needle) != std::wstring::npos;
}

void sleep_ms(DWORD ms) {
    std::this_thread::sleep_for(std::chrono::milliseconds(ms));
}

// ---------------------------------------------------------------------------
// Fake host
// ---------------------------------------------------------------------------

enum class ExportMode {
    kAsync,         // SAVE, then EDIT from a worker thread; writes a 1024-byte file
    kWritesNothing, // SAVE then EDIT from a worker thread; no output file
    kNeverStarts,   // output_file() returns true, state never changes
    kHangs,         // SAVE inside output_file() and never returns to EDIT
    kStaleFile,     // SAVE then EDIT, but the output file was not written by this export
};

std::atomic<int> t_state{kStateEdit};
ExportMode t_mode = ExportMode::kAsync;
bool t_output_returns = true;
bool t_save_returns = true;
bool t_open_retimed_fails = false;
bool t_mismatch_retimed = false;  // the retimed project reports another path after loading
bool t_confirm_answer = true;     // what the save prompt returns
std::string t_saved_text;         // what save_project_file writes to the project's own path
std::wstring t_loaded_copy;       // storage behind get_project_file_path()
std::vector<std::wstring> t_opened;
std::vector<std::wstring> t_saved_to;
std::vector<std::wstring> t_confirm_asked;
std::vector<std::wstring> t_output_files;
std::vector<std::thread> t_workers;
void (*t_listener)(void*) = nullptr;
void* t_listener_param = nullptr;
void (*t_load_handler)(PROJECT_FILE*) = nullptr;
void (*t_save_handler)(PROJECT_FILE*) = nullptr;
void (*t_menu)(void*) = nullptr;
const wchar_t* t_menu_name = nullptr;
std::vector<std::wstring> t_notify_texts;
std::vector<bool> t_notify_ok;

EDIT_HANDLE t_handle{};
PROJECT_FILE t_project{};
HOST_APP_TABLE t_host{};

void notify_listener() {
    if (t_listener != nullptr) {
        t_listener(t_listener_param);
    }
}

void record_notify(bool ok, const std::wstring& text) {
    t_notify_ok.push_back(ok);
    t_notify_texts.push_back(text);
}

bool record_confirm(const std::wstring& path) {
    t_confirm_asked.push_back(path);
    return t_confirm_answer;
}

EDIT_HANDLE* fake_create_edit_handle() {
    return &t_handle;
}

void fake_register_project_load(void (*func)(PROJECT_FILE*)) {
    t_load_handler = func;
}

void fake_register_project_save(void (*func)(PROJECT_FILE*)) {
    t_save_handler = func;
}

void fake_register_event(EVENT_TYPE type, void* param, void (*func)(void*)) {
    if (type == EVENT_TYPE::CHANGE_EDIT_STATE) {
        t_listener = func;
        t_listener_param = param;
    }
}

void fake_register_edit_menu(LPCWSTR name, void*, void (*func)(void*)) {
    t_menu = func;
    t_menu_name = name;
}

int fake_get_edit_state() {
    return t_state.load();
}

HWND fake_host_window() {
    return nullptr;
}

bool fake_save_project_file(LPCWSTR file) {
    t_saved_to.push_back(file);
    if (!t_save_returns) {
        return false;
    }
    return write_file_bytes(file, t_saved_text);
}

bool fake_open_project_file(LPCWSTR file, bool show_confirm) {
    t_opened.push_back(file);
    if (show_confirm) {
        return false;  // the plugin must never ask
    }
    const std::wstring name = file;
    const bool is_retimed = name.find(L"retimed.aup2") != std::wstring::npos;
    if (is_retimed && t_open_retimed_fails) {
        return false;
    }
    t_loaded_copy = (is_retimed && t_mismatch_retimed) ? name + L".other" : name;
    if (t_load_handler != nullptr) {
        t_load_handler(&t_project);
    }
    return true;
}

LPCWSTR fake_project_path() {
    return t_loaded_copy.empty() ? nullptr : t_loaded_copy.c_str();
}

bool fake_output_file(LPCWSTR file, LPCWSTR, void* param, void (*func_project_config)(void*, PROJECT_FILE*)) {
    t_output_files.push_back(file);
    if (param != nullptr || func_project_config != nullptr) {
        return false;  // the plugin passes nullptr for both
    }
    if (!t_output_returns) {
        return false;
    }
    const std::wstring path = file;
    switch (t_mode) {
    case ExportMode::kAsync:
    case ExportMode::kStaleFile:
        t_workers.emplace_back([path]() {
            sleep_ms(40);
            t_state = kStateSave;
            notify_listener();
            sleep_ms(40);
            if (t_mode == ExportMode::kAsync) {
                write_file_bytes(path, std::string(1024, 'x'));
            }
            t_state = kStateEdit;
            notify_listener();
        });
        return true;
    case ExportMode::kWritesNothing:
        t_workers.emplace_back([]() {
            sleep_ms(40);
            t_state = kStateSave;
            notify_listener();
            sleep_ms(40);
            t_state = kStateEdit;
            notify_listener();
        });
        return true;
    case ExportMode::kNeverStarts:
        return true;
    case ExportMode::kHangs:
        t_state = kStateSave;
        notify_listener();
        return true;
    }
    return false;
}

void join_workers() {
    for (auto& worker : t_workers) {
        if (worker.joinable()) {
            worker.join();
        }
    }
    t_workers.clear();
}

void reset_fake(ExportMode mode) {
    join_workers();
    UninitializePlugin();
    t_state = kStateEdit;
    t_mode = mode;
    t_output_returns = true;
    t_save_returns = true;
    t_open_retimed_fails = false;
    t_mismatch_retimed = false;
    t_confirm_answer = true;
    t_opened.clear();
    t_saved_to.clear();
    t_confirm_asked.clear();
    t_output_files.clear();
    t_notify_texts.clear();
    t_notify_ok.clear();
    g_timing = Timing();
    g_timing.tick_ms = 10;
    g_timing.ready_timeout_ms = 2000;
    g_timing.load_timeout_ms = 2000;
    g_timing.export_start_timeout_ms = 300;
    g_timing.export_timeout_ms = 2000;
    g_notify = record_notify;
    g_confirm_save = record_confirm;
    SetEnvironmentVariableW(kKeepTempEnv, nullptr);
}

// ---------------------------------------------------------------------------
// Driving the plugin
// ---------------------------------------------------------------------------

bool pump_until_idle() {
    const ULONGLONG deadline = GetTickCount64() + 30000;
    while (g_run) {
        MSG msg;
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        if (!g_run) {
            break;
        }
        if (GetTickCount64() > deadline) {
            return false;
        }
        Sleep(5);
    }
    return true;
}

std::wstring g_root;

std::wstring case_dir(const std::wstring& name) {
    const std::wstring dir = g_root + name + L"\\";
    CreateDirectoryW(dir.c_str(), nullptr);
    return dir;
}

// Number of entries (files and folders) directly inside a folder, excluding . and ..
int entry_count(const std::wstring& dir) {
    WIN32_FIND_DATAW data{};
    HANDLE find = FindFirstFileW((dir + L"*").c_str(), &data);
    if (find == INVALID_HANDLE_VALUE) {
        return 0;
    }
    int count = 0;
    do {
        if (wcscmp(data.cFileName, L".") != 0 && wcscmp(data.cFileName, L"..") != 0) {
            ++count;
        }
    } while (FindNextFileW(find, &data));
    FindClose(find);
    return count;
}

std::wstring work_root() {
    return g_root + L"work\\";
}

// Removes a kept temp folder (files, then the folder) written by a run.
void remove_kept_dir(const std::wstring& dir) {
    for (const wchar_t* name : {L"src.aup2", L"retimed.aup2", L"retimed.aup2.convert.json"}) {
        DeleteFileW((dir + name).c_str());
    }
    RemoveDirectoryW(dir.c_str());
}

std::string read_text(const std::wstring& path) {
    std::ifstream in(path, std::ios::binary);
    std::ostringstream text;
    text << in.rdbuf();
    return text.str();
}

bool file_exists(const std::wstring& path) {
    return GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES;
}

// A project on disk, loaded in the host (the load callback sets the project path).
struct Case {
    std::wstring dir;
    std::wstring original;
    std::wstring output;
};

std::string g_fixture30;

Case load_case(const std::wstring& name) {
    Case c;
    c.dir = case_dir(name);
    c.original = c.dir + L"project.aup2";
    c.output = c.dir + L"out.mp4";
    write_file_bytes(c.original, g_fixture30);
    t_loaded_copy = c.original;
    if (t_load_handler != nullptr) {
        t_load_handler(&t_project);
    }
    return c;
}

void remove_case(const Case& c) {
    DeleteFileW(c.original.c_str());
    DeleteFileW(c.output.c_str());
    RemoveDirectoryW(c.dir.c_str());
}

// Runs the menu path after the dialog: begin_export, then pump until the run ends.
bool run_export(const Case& c, Rational target) {
    ExportRequest request;
    request.target = target;
    request.output = c.output;
    request.plugin = L"Test Output";
    if (!begin_export(request)) {
        return false;
    }
    return pump_until_idle();
}

bool opened_exactly(const std::vector<std::wstring>& expected) {
    return t_opened == expected;
}

std::wstring last_text() {
    return t_notify_texts.empty() ? std::wstring() : t_notify_texts.back();
}

// ---------------------------------------------------------------------------
// Scenarios
// ---------------------------------------------------------------------------

void scenario_no_path() {
    reset_fake(ExportMode::kAsync);
    const Case c = load_case(L"no_path");
    t_loaded_copy.clear();
    if (t_load_handler != nullptr) {
        t_load_handler(&t_project);  // a new, never saved project reports no path
    }
    const bool started = run_export(c, Rational{66, 1});
    check(!started && t_confirm_asked.empty() && t_saved_to.empty() && t_opened.empty(),
          "no path: refused before the prompt, nothing saved or opened");
    check(!t_notify_texts.empty() && contains(last_text(), L"has no file yet") && !t_notify_ok.back(),
          "no path: message asks to save first", narrow(last_text()));
    check(entry_count(work_root()) == 0, "no path: no temp folder created");
    remove_case(c);
}

void scenario_prompt_cancel() {
    reset_fake(ExportMode::kAsync);
    const Case c = load_case(L"prompt_cancel");
    t_confirm_answer = false;
    const bool started = run_export(c, Rational{66, 1});
    check(!started, "prompt cancel: no run starts");
    check(t_confirm_asked.size() == 1 && t_confirm_asked[0] == c.original,
          "prompt cancel: prompt shows the project's own path");
    check(t_saved_to.empty() && t_opened.empty() && t_output_files.empty(),
          "prompt cancel: no save, no open, no output");
    check(read_text(c.original) == g_fixture30 && t_notify_texts.empty(),
          "prompt cancel: project file untouched and no message");
    check(entry_count(work_root()) == 0, "prompt cancel: no temp folder created");
    remove_case(c);
}

void scenario_success() {
    reset_fake(ExportMode::kAsync);
    const Case c = load_case(L"success");
    t_saved_text = g_fixture30;
    const bool finished = run_export(c, Rational{66, 1});
    check(finished, "success: run finishes");
    check(t_confirm_asked.size() == 1 && t_saved_to.size() == 1 && t_saved_to[0] == c.original,
          "success: project saved to its own path after the prompt");
    check(read_text(c.original) == t_saved_text, "success: project file holds the saved content");
    check(t_opened.size() == 2 && t_opened[1] == c.original && t_opened[0].find(L"retimed.aup2") != std::wstring::npos,
          "success: retimed opened, then the ORIGINAL path reopened", narrow(t_opened.empty() ? L"" : t_opened.back()));
    check(!t_notify_ok.empty() && t_notify_ok.back() && contains(last_text(), L"Export finished") &&
              contains(last_text(), L"Size: 1024 bytes") && contains(last_text(), L"Rate: 66/1") &&
              contains(last_text(), L"The original project was reopened"),
          "success: result is OK with size, rate and reopen note", narrow(last_text()));
    check(entry_count(work_root()) == 0, "success: temp folder removed");
    remove_case(c);
}

// A failure after the user confirmed the save. expect_retimed_open: the retimed copy was opened before the failure.
// In both cases the original path must be the last project opened.
void scenario_failure(const char* name, const std::wstring& case_name, ExportMode mode,
                      void (*setup)(), const std::wstring& reason, bool expect_retimed_open) {
    reset_fake(mode);
    const Case c = load_case(case_name);
    t_saved_text = g_fixture30;
    setup();
    run_export(c, Rational{66, 1});
    const std::size_t expected_opens = expect_retimed_open ? 2 : 1;
    const bool sequence_ok = t_opened.size() == expected_opens && t_opened.back() == c.original &&
                             (!expect_retimed_open || t_opened[0].find(L"retimed.aup2") != std::wstring::npos);
    check(!t_notify_ok.empty() && !t_notify_ok.back(), std::string(name) + ": result is FAILED");
    check(contains(last_text(), reason), std::string(name) + ": reason shown", narrow(last_text()));
    check(sequence_ok, std::string(name) + ": the ORIGINAL path is reopened last",
          t_opened.empty() ? "nothing opened" : narrow(t_opened.back()));
    check(entry_count(work_root()) == 0, std::string(name) + ": temp folder removed");
    remove_case(c);
}

void setup_none() {}
void setup_save_fails() {
    t_save_returns = false;
}
void setup_open_retimed_fails() {
    t_open_retimed_fails = true;
}
void setup_mismatch() {
    t_mismatch_retimed = true;
}
void setup_output_refused() {
    t_output_returns = false;
}
void setup_refused() {
    t_saved_text = g_fixture30 + "コマ落ち=1\n";
}
void setup_stale_output() {}

void scenario_save_fails() {
    scenario_failure("save fails", L"save_fails", ExportMode::kAsync, setup_save_fails,
                     L"save_project_file returned false", false);
}

void scenario_refused() {
    reset_fake(ExportMode::kAsync);
    const Case c = load_case(L"refused");
    t_saved_text = g_fixture30 + "コマ落ち=1\n";
    run_export(c, Rational{66, 1});
    check(!t_notify_ok.empty() && !t_notify_ok.back() && contains(last_text(), L"REFUSED: line ") &&
              contains(last_text(), L"コマ落ち=1 is not supported"),
          "refused: result carries the line number", narrow(last_text()));
    check(t_opened.size() == 1 && t_opened[0] == c.original,
          "refused: the project is saved, no retimed copy opened, and the original reopened");
    check(entry_count(work_root()) == 0, "refused: temp folder removed");
    remove_case(c);
}

void scenario_open_retimed_fails() {
    scenario_failure("open fails", L"open_fails", ExportMode::kAsync, setup_open_retimed_fails,
                     L"open_project_file returned false", true);
}

void scenario_load_mismatch() {
    scenario_failure("load mismatch", L"load_mismatch", ExportMode::kAsync, setup_mismatch,
                     L"differs from the retimed copy", true);
}

void scenario_output_refused() {
    scenario_failure("output_file false", L"output_refused", ExportMode::kAsync, setup_output_refused,
                     L"output_file returned false", true);
}

void scenario_never_starts() {
    scenario_failure("export never starts", L"never_starts", ExportMode::kNeverStarts, setup_none,
                     L"export start (CHANGE_EDIT_STATE) was not observed", true);
}

void scenario_no_output_file() {
    scenario_failure("no output file", L"no_output", ExportMode::kWritesNothing, setup_none,
                     L"output file missing after export", true);
}

void scenario_stale_output() {
    reset_fake(ExportMode::kStaleFile);
    const Case c = load_case(L"stale");
    t_saved_text = g_fixture30;
    write_file_bytes(c.output, std::string(2048, 'o'));
    HANDLE file = CreateFileW(c.output.c_str(), FILE_WRITE_ATTRIBUTES, 0, nullptr, OPEN_EXISTING, 0, nullptr);
    if (file != INVALID_HANDLE_VALUE) {
        FILETIME old{};
        SYSTEMTIME st{};
        st.wYear = 2000;
        st.wMonth = 1;
        st.wDay = 1;
        SystemTimeToFileTime(&st, &old);
        SetFileTime(file, nullptr, nullptr, &old);
        CloseHandle(file);
    }
    run_export(c, Rational{66, 1});
    check(!t_notify_ok.empty() && !t_notify_ok.back() && contains(last_text(), L"was not written by this export"),
          "stale output: FAILED (old file not accepted)", narrow(last_text()));
    check(!t_opened.empty() && t_opened.back() == c.original, "stale output: the ORIGINAL path is reopened last");
    check(entry_count(work_root()) == 0, "stale output: temp folder removed");
    remove_case(c);
}

void scenario_hangs() {
    reset_fake(ExportMode::kHangs);
    const Case c = load_case(L"hangs");
    t_saved_text = g_fixture30;
    g_timing.export_timeout_ms = 400;
    run_export(c, Rational{66, 1});
    check(!t_notify_ok.empty() && !t_notify_ok.back() && contains(last_text(), L"the export timed out") &&
              contains(last_text(), L"was not reopened"),
          "hangs: FAILED on timeout; original not reopened while the host is busy", narrow(last_text()));
    check(t_opened.size() == 1 && t_opened[0].find(L"retimed.aup2") != std::wstring::npos,
          "hangs: only the retimed copy was opened; no reopen while the host is busy");
    t_state = kStateEdit;
    join_workers();
    check(entry_count(work_root()) == 0, "hangs: temp folder removed");
    remove_case(c);
}

void scenario_success_keep_temp() {
    reset_fake(ExportMode::kAsync);
    const Case c = load_case(L"keep_temp");
    t_saved_text = g_fixture30;
    SetEnvironmentVariableW(kKeepTempEnv, L"1");
    const bool finished = run_export(c, Rational{66, 1});
    SetEnvironmentVariableW(kKeepTempEnv, nullptr);
    check(finished && !t_notify_ok.empty() && t_notify_ok.back(), "keep temp: run OK");
    check(entry_count(work_root()) == 1, "keep temp: temp folder kept when FREE_FPS_EXPORT_KEEP_TEMP=1",
          std::to_string(entry_count(work_root())));
    check(contains(last_text(), L"Temp files kept"), "keep temp: result names the kept folder");
    // Remove the kept folder: the only entry under the work root.
    WIN32_FIND_DATAW data{};
    HANDLE find = FindFirstFileW((work_root() + L"*").c_str(), &data);
    if (find != INVALID_HANDLE_VALUE) {
        do {
            if (wcscmp(data.cFileName, L".") != 0 && wcscmp(data.cFileName, L"..") != 0) {
                remove_kept_dir(work_root() + data.cFileName + L"\\");
            }
        } while (FindNextFileW(find, &data));
        FindClose(find);
    }
    remove_case(c);
}

void scenario_busy_second_start() {
    reset_fake(ExportMode::kAsync);
    const Case c = load_case(L"busy");
    t_saved_text = g_fixture30;
    ExportRequest request;
    request.target = Rational{66, 1};
    request.output = c.output;
    request.plugin = L"Test Output";
    const bool first = begin_export(request);
    const bool second = begin_export(request);
    check(first && !second && g_run != nullptr && contains(last_text(), L"already running"),
          "busy: a second export is refused while a run is active");
    pump_until_idle();
    check(entry_count(work_root()) == 0, "busy: temp folder removed after the first run");
    remove_case(c);
}

void scenario_menu_registered() {
    reset_fake(ExportMode::kAsync);
    check(t_menu != nullptr && t_menu_name != nullptr && std::wstring(t_menu_name) == L"Free FPS Export...",
          "menu: Edit > Free FPS Export... is registered");
}

}  // namespace

int main(int argc, char** argv) {
    std::string fixture_dir;
    if (argc > 1) {
        fixture_dir = argv[1];
    } else {
        std::string here = __FILE__;
        const std::size_t cut = here.find_last_of("\\/");
        here = here.substr(0, cut == std::string::npos ? 0 : cut);
        fixture_dir = here + "\\..\\..\\66_to_60_validation";
    }
    {
        std::ifstream in(fixture_dir + "\\freerenderfps_30fps.aup2", std::ios::binary);
        std::ostringstream text;
        text << in.rdbuf();
        g_fixture30 = text.str();
    }
    check(!g_fixture30.empty(), "setup: 30 FPS fixture readable");

    // Wire the fake host.
    t_handle.get_edit_state = fake_get_edit_state;
    t_handle.get_host_app_window = fake_host_window;
    t_handle.save_project_file = fake_save_project_file;
    t_handle.open_project_file = fake_open_project_file;
    t_handle.output_file = fake_output_file;
    t_project.get_project_file_path = fake_project_path;
    t_host.create_edit_handle = fake_create_edit_handle;
    t_host.register_project_load_handler = fake_register_project_load;
    t_host.register_project_save_handler = fake_register_project_save;
    t_host.register_event_listener = fake_register_event;
    t_host.register_edit_menu_param = fake_register_edit_menu;

    wchar_t temp[MAX_PATH + 1] = {};
    GetTempPathW(MAX_PATH + 1, temp);
    g_root = std::wstring(temp) + L"FreeFpsExportTest\\" + std::to_wstring(GetCurrentProcessId()) + L"\\";
    CreateDirectoryW((std::wstring(temp) + L"FreeFpsExportTest\\").c_str(), nullptr);
    CreateDirectoryW(g_root.c_str(), nullptr);
    CreateDirectoryW(work_root().c_str(), nullptr);
    g_work_root = work_root();

    RegisterPlugin(&t_host);
    scenario_menu_registered();
    scenario_no_path();
    scenario_prompt_cancel();
    scenario_success();
    scenario_save_fails();
    scenario_refused();
    scenario_open_retimed_fails();
    scenario_load_mismatch();
    scenario_output_refused();
    scenario_never_starts();
    scenario_no_output_file();
    scenario_stale_output();
    scenario_hangs();
    scenario_success_keep_temp();
    scenario_busy_second_start();

    join_workers();
    UninitializePlugin();
    RemoveDirectoryW(work_root().c_str());
    RemoveDirectoryW(g_root.c_str());
    RemoveDirectoryW((std::wstring(temp) + L"FreeFpsExportTest\\").c_str());

    std::printf("%s (%d failure%s)\n", g_failures == 0 ? "ALL PASS" : "FAILED", g_failures,
                g_failures == 1 ? "" : "s");
    return g_failures == 0 ? 0 : 1;
}
