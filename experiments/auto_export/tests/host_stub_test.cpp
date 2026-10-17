// Host-free test for experiments/auto_export/auto_export.cpp.
//
// The plugin source is included directly so the timing limits and the run state
// can be reached from a fake host (HOST_APP_TABLE, EDIT_HANDLE, PROJECT_FILE).
// No AviUtl window is created. The test only pumps this thread's message queue
// so the plugin's thread timer fires.
//
// Build and run: experiments\auto_export\build_and_run.ps1

#include "../auto_export.cpp"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <thread>
#include <vector>

namespace {

using Row = std::vector<std::string>;
using Table = std::vector<Row>;

int g_failures = 0;

void check(bool condition, const std::string& name, const std::string& detail = std::string()) {
    if (condition) {
        std::printf("PASS %s\n", name.c_str());
    } else {
        std::printf("FAIL %s%s%s\n", name.c_str(), detail.empty() ? "" : " : ", detail.c_str());
        ++g_failures;
    }
}

void sleep_ms(DWORD ms) {
    std::this_thread::sleep_for(std::chrono::milliseconds(ms));
}

// ---------------------------------------------------------------------------
// Fake host state. Touched only from the main thread, except g_fake_state and
// the listener call made from the export worker threads.
// ---------------------------------------------------------------------------

enum class ExportMode {
    kAsync,         // SAVE, then EDIT from a worker thread; writes a 1024-byte file
    kSync,          // SAVE and EDIT inside output_file(); writes a 1024-byte file
    kWritesNothing, // SAVE then EDIT from a worker thread; no output file
    kNeverStarts,   // output_file() returns true, state never changes
    kHangs,         // SAVE inside output_file() and never returns to EDIT
};

std::atomic<int> g_fake_state{kStateEdit};
ExportMode g_mode = ExportMode::kAsync;
bool g_output_returns = true;
std::wstring g_loaded;
std::wstring g_loaded_copy;  // storage behind get_project_file_path()
std::vector<std::wstring> g_opened;
bool g_show_confirm_seen = false;
std::vector<std::wstring> g_output_files;
std::vector<std::wstring> g_output_plugins;
int g_info_size = -1;
int g_output_args_nonnull = 0;
void (*g_listener)(void*) = nullptr;
void* g_listener_param = nullptr;
void (*g_load_handler)(PROJECT_FILE*) = nullptr;
void (*g_menu)(void*) = nullptr;
std::vector<std::thread> g_workers;

EDIT_HANDLE g_handle{};
PROJECT_FILE g_project{};
HOST_APP_TABLE g_host{};

void notify_listener() {
    if (g_listener != nullptr) {
        g_listener(g_listener_param);
    }
}

bool write_bytes(const std::wstring& path, const std::string& bytes) {
    HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                              FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        return false;
    }
    DWORD written = 0;
    const BOOL ok = WriteFile(file, bytes.data(), static_cast<DWORD>(bytes.size()), &written, nullptr);
    CloseHandle(file);
    return ok && written == bytes.size();
}

std::string read_text(const std::wstring& path) {
    std::string bytes;
    read_file_bytes(path, bytes);
    return bytes;
}

Table parse_table(const std::string& text) {
    Table table;
    size_t pos = 0;
    while (pos < text.size()) {
        size_t end = text.find('\n', pos);
        if (end == std::string::npos) {
            end = text.size();
        }
        const std::string line = text.substr(pos, end - pos);
        pos = end + 1;
        Row row;
        size_t start = 0;
        for (;;) {
            const size_t tab = line.find('\t', start);
            row.push_back(line.substr(start, tab == std::string::npos ? std::string::npos : tab - start));
            if (tab == std::string::npos) {
                break;
            }
            start = tab + 1;
        }
        table.push_back(row);
    }
    return table;
}

void join_workers() {
    for (auto& worker : g_workers) {
        if (worker.joinable()) {
            worker.join();
        }
    }
    g_workers.clear();
}

void reset_fake(ExportMode mode) {
    join_workers();
    UninitializePlugin();  // kills any timer and drops a leftover run
    g_fake_state = kStateEdit;
    g_mode = mode;
    g_output_returns = true;
    g_loaded.clear();
    g_loaded_copy.clear();
    g_opened.clear();
    g_show_confirm_seen = false;
    g_output_files.clear();
    g_output_plugins.clear();
    g_info_size = -1;
    g_output_args_nonnull = 0;
    g_timing = Timing();
}

EDIT_HANDLE* fake_create_edit_handle() {
    return &g_handle;
}

void fake_register_project_load(void (*func)(PROJECT_FILE*)) {
    g_load_handler = func;
}

void fake_register_event(EVENT_TYPE type, void* param, void (*func)(void*)) {
    if (type == EVENT_TYPE::CHANGE_EDIT_STATE) {
        g_listener = func;
        g_listener_param = param;
    }
}

void fake_register_edit_menu(LPCWSTR, void*, void (*func)(void*)) {
    g_menu = func;
}

int fake_get_edit_state() {
    return g_fake_state.load();
}

void fake_get_edit_info(EDIT_INFO* info, int size) {
    g_info_size = size;
    info->rate = 30000;
    info->scale = 1001;
    info->frame_max = 149;
    info->width = 640;
    info->height = 360;
}

bool fake_open_project_file(LPCWSTR file, bool show_confirm) {
    g_opened.push_back(file);
    g_show_confirm_seen = g_show_confirm_seen || show_confirm;
    const std::wstring name = file;
    if (name.find(L"open_fail") != std::wstring::npos) {
        return false;
    }
    g_loaded = name.find(L"mismatch") != std::wstring::npos ? name + L".other" : name;
    g_loaded_copy = g_loaded;
    if (name.find(L"noload") == std::wstring::npos && g_load_handler != nullptr) {
        g_load_handler(&g_project);
    }
    return true;
}

LPCWSTR fake_project_path() {
    return g_loaded_copy.c_str();
}

bool fake_output_file(LPCWSTR file, LPCWSTR plugin, void* param, void (*func_project_config)(void*, PROJECT_FILE*)) {
    g_output_files.push_back(file);
    g_output_plugins.push_back(plugin);
    if (param != nullptr || func_project_config != nullptr) {
        ++g_output_args_nonnull;
    }
    if (!g_output_returns) {
        return false;
    }
    const std::wstring path = file;
    switch (g_mode) {
    case ExportMode::kSync:
        g_fake_state = kStateSave;
        notify_listener();
        write_bytes(path, std::string(1024, 'x'));
        g_fake_state = kStateEdit;
        notify_listener();
        return true;
    case ExportMode::kAsync:
        g_workers.emplace_back([path]() {
            sleep_ms(40);
            g_fake_state = kStateSave;
            notify_listener();
            sleep_ms(40);
            write_bytes(path, std::string(1024, 'x'));
            g_fake_state = kStateEdit;
            notify_listener();
        });
        return true;
    case ExportMode::kWritesNothing:
        g_workers.emplace_back([]() {
            sleep_ms(40);
            g_fake_state = kStateSave;
            notify_listener();
            sleep_ms(40);
            g_fake_state = kStateEdit;
            notify_listener();
        });
        return true;
    case ExportMode::kNeverStarts:
        return true;
    case ExportMode::kHangs:
        g_fake_state = kStateSave;
        notify_listener();
        return true;
    }
    return false;
}

// ---------------------------------------------------------------------------
// Driving the plugin.
// ---------------------------------------------------------------------------

std::string job_line(const std::wstring& project, const std::wstring& output, const std::wstring& plugin) {
    return wide_to_utf8(project + L"\t" + output + L"\t" + plugin) + "\n";
}

// Pumps this thread's message queue (where the plugin's timer fires) until the
// run ends. Returns false if it did not end within the deadline.
bool pump_until_idle() {
    const ULONGLONG deadline = GetTickCount64() + 20000;
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

// Writes the job file, invokes the menu command and runs until the run ends.
bool start_and_pump(const std::wstring& job_path, const std::string& text) {
    if (!write_bytes(job_path, text)) {
        return false;
    }
    SetEnvironmentVariableW(L"AUTO_EXPORT_JOBS", job_path.c_str());
    g_menu(nullptr);
    return pump_until_idle();
}

std::wstring results_path_for(const std::wstring& job_path) {
    return job_path + L".results.tsv";
}

Table results_of(const std::wstring& job_path) {
    return parse_table(read_text(results_path_for(job_path)));
}

// Columns: 0 index, 1 status, 2 project, 3 output, 4 plugin, 5 rate, 6 scale,
// 7 frame_max, 8 width, 9 height, 10 output_bytes, 11 elapsed_ms, 12 reason.
constexpr size_t kColumns = 13;

bool row_has_full_columns(const Table& table, size_t row) {
    return table.size() > row && table[row].size() == kColumns;
}

void scenario_async_two_jobs(const std::wstring& work) {
    reset_fake(ExportMode::kAsync);
    const std::wstring job_path = work + L"jobs_async.txt";
    const std::wstring out_a = work + L"out_a.mp4";
    const std::wstring out_b = work + L"out_b.mp4";
    const std::string text = "# comment line\n\n" +
                             job_line(work + L"ok_a.aup2", out_a, L"Test Output") +
                             job_line(work + L"ok_b.aup2", out_b, L"Test Output");

    write_bytes(job_path, text);
    SetEnvironmentVariableW(L"AUTO_EXPORT_JOBS", job_path.c_str());
    g_menu(nullptr);
    const bool run_started = g_run != nullptr;
    g_menu(nullptr);  // second command while active must not start another run
    const bool still_one_run = g_run != nullptr;
    const bool finished = pump_until_idle();

    check(run_started, "async: menu command starts a run");
    check(still_one_run, "async: second menu command does not replace the active run");
    check(finished, "async: run finishes");

    const Table table = results_of(job_path);
    check(read_text(results_path_for(job_path)).rfind(wide_to_utf8(kResultsHeader), 0) == 0,
          "async: results header line");
    check(row_has_full_columns(table, 1) && row_has_full_columns(table, 2), "async: two data rows with 13 columns");
    if (table.size() >= 3) {
        check(table[1][1] == "OK" && table[2][1] == "OK", "async: both jobs OK");
        check(table[1][5] == "30000" && table[1][6] == "1001" && table[1][7] == "149" &&
                  table[1][8] == "640" && table[1][9] == "360",
              "async: scene rate, scale, frame_max, width, height recorded");
        check(std::atoll(table[1][10].c_str()) == 1024 && std::atoll(table[2][10].c_str()) == 1024,
              "async: output_bytes recorded from the output file");
        check(table[1][12].empty() && table[2][12].empty(), "async: OK rows have empty reason");
    }
    check(g_opened.size() == 2 && g_opened[0] == work + L"ok_a.aup2" && g_opened[1] == work + L"ok_b.aup2",
          "async: projects opened in job order without confirm dialog");
    check(!g_show_confirm_seen, "async: open_project_file called with show_confirm=false");
    check(g_output_files.size() == 2 && g_output_files[0] == out_a && g_output_files[1] == out_b,
          "async: output_file called once per job with the job output path");
    check(g_output_plugins.size() == 2 && g_output_plugins[0] == L"Test Output",
          "async: output plugin name passed through");
    check(g_output_args_nonnull == 0, "async: output_file gets nullptr param and config callback");
    check(g_info_size == static_cast<int>(sizeof(EDIT_INFO)), "async: get_edit_info sized with sizeof(EDIT_INFO)");
    check(!g_run, "async: run cleared after completion");
}

void scenario_sync_export(const std::wstring& work) {
    reset_fake(ExportMode::kSync);
    const std::wstring job_path = work + L"jobs_sync.txt";
    const std::wstring out = work + L"out_sync.mp4";
    const bool finished = start_and_pump(job_path, job_line(work + L"ok_sync.aup2", out, L"Test Output"));
    const Table table = results_of(job_path);
    check(finished, "sync: run finishes");
    check(row_has_full_columns(table, 1) && table[1][1] == "OK",
          "sync: SAVE and EDIT inside output_file() are still observed");
}

void scenario_open_failure_continues(const std::wstring& work) {
    reset_fake(ExportMode::kAsync);
    const std::wstring job_path = work + L"jobs_open_fail.txt";
    const std::string text =
        job_line(work + L"open_fail.aup2", work + L"out_c.mp4", L"Test Output") +
        job_line(work + L"ok_b.aup2", work + L"out_b2.mp4", L"Test Output");
    const bool finished = start_and_pump(job_path, text);
    const Table table = results_of(job_path);
    check(finished, "open failure: run finishes");
    check(row_has_full_columns(table, 1) && table[1][1] == "FAILED" &&
              table[1][12] == "open_project_file returned false",
          "open failure: first job FAILED with reason");
    check(row_has_full_columns(table, 2) && table[2][1] == "OK", "open failure: next job still runs and is OK");
    check(g_output_files.size() == 1, "open failure: failed job never reaches output_file");
}

void scenario_output_refused(const std::wstring& work) {
    reset_fake(ExportMode::kAsync);
    g_output_returns = false;
    const std::wstring job_path = work + L"jobs_output_refused.txt";
    const bool finished = start_and_pump(job_path, job_line(work + L"ok_r.aup2", work + L"out_r.mp4", L"Test Output"));
    const Table table = results_of(job_path);
    check(finished, "output refused: run finishes");
    check(row_has_full_columns(table, 1) && table[1][1] == "FAILED" &&
              table[1][12] == "output_file returned false",
          "output refused: FAILED with reason");
}

void scenario_missing_output_file(const std::wstring& work) {
    reset_fake(ExportMode::kWritesNothing);
    const std::wstring job_path = work + L"jobs_no_file.txt";
    const std::wstring out = work + L"out_missing.mp4";
    DeleteFileW(out.c_str());
    const bool finished = start_and_pump(job_path, job_line(work + L"ok_n.aup2", out, L"Test Output"));
    const Table table = results_of(job_path);
    check(finished, "missing output: run finishes");
    check(row_has_full_columns(table, 1) && table[1][1] == "FAILED" &&
              table[1][12] == "output file missing or empty after export" && table[1][10] == "-",
          "missing output: FAILED, output_bytes '-'");
}

void scenario_load_not_observed(const std::wstring& work) {
    reset_fake(ExportMode::kAsync);
    g_timing.load_timeout_ms = 300;
    const std::wstring job_path = work + L"jobs_noload.txt";
    const bool finished = start_and_pump(job_path, job_line(work + L"noload.aup2", work + L"out_n.mp4", L"Test Output"));
    const Table table = results_of(job_path);
    check(finished, "load timeout: run finishes");
    check(row_has_full_columns(table, 1) && table[1][1] == "FAILED" &&
              table[1][12] == "project load callback not observed",
          "load timeout: FAILED with reason");
    check(g_output_files.empty(), "load timeout: output_file not called");
}

void scenario_loaded_project_mismatch(const std::wstring& work) {
    reset_fake(ExportMode::kAsync);
    const std::wstring job_path = work + L"jobs_mismatch.txt";
    const bool finished = start_and_pump(job_path, job_line(work + L"mismatch.aup2", work + L"out_m.mp4", L"Test Output"));
    const Table table = results_of(job_path);
    check(finished, "mismatch: run finishes");
    check(row_has_full_columns(table, 1) && table[1][1] == "FAILED" &&
              table[1][12].rfind("loaded project differs: ", 0) == 0,
          "mismatch: FAILED when another project is loaded");
    check(g_output_files.empty(), "mismatch: output_file not called");
}

void scenario_export_start_not_observed(const std::wstring& work) {
    reset_fake(ExportMode::kNeverStarts);
    g_timing.export_start_timeout_ms = 300;
    const std::wstring job_path = work + L"jobs_never_starts.txt";
    const std::string text = job_line(work + L"ok_s1.aup2", work + L"out_s1.mp4", L"Test Output") +
                             job_line(work + L"ok_s2.aup2", work + L"out_s2.mp4", L"Test Output");
    const bool finished = start_and_pump(job_path, text);
    const Table table = results_of(job_path);
    check(finished, "export start: run finishes");
    check(row_has_full_columns(table, 1) && table[1][1] == "FAILED" &&
              table[1][12] == "export start (CHANGE_EDIT_STATE) not observed",
          "export start: first job FAILED with reason");
    check(row_has_full_columns(table, 2) && table[2][1] == "SKIPPED" &&
              table[2][12] == "run aborted: export start (CHANGE_EDIT_STATE) not observed",
          "export start: remaining job SKIPPED after abort");
    check(g_output_files.size() == 1, "export start: second job never started");
}

void scenario_export_timeout(const std::wstring& work) {
    reset_fake(ExportMode::kHangs);
    g_timing.export_timeout_ms = 500;
    const std::wstring job_path = work + L"jobs_hang.txt";
    const std::string text = job_line(work + L"ok_h1.aup2", work + L"out_h1.mp4", L"Test Output") +
                             job_line(work + L"ok_h2.aup2", work + L"out_h2.mp4", L"Test Output");
    const bool finished = start_and_pump(job_path, text);
    const Table table = results_of(job_path);
    check(finished, "export timeout: run finishes");
    check(row_has_full_columns(table, 1) && table[1][1] == "FAILED" && table[1][12] == "export timeout",
          "export timeout: first job FAILED with reason");
    check(row_has_full_columns(table, 2) && table[2][1] == "SKIPPED", "export timeout: remaining job SKIPPED");
    check(g_output_files.size() == 1, "export timeout: second job never started");
}

void scenario_host_busy(const std::wstring& work) {
    reset_fake(ExportMode::kAsync);
    g_fake_state = kStateSave;  // host is still exporting before the run starts
    g_timing.ready_timeout_ms = 300;
    const std::wstring job_path = work + L"jobs_busy.txt";
    const std::string text = job_line(work + L"ok_b1.aup2", work + L"out_b1.mp4", L"Test Output") +
                             job_line(work + L"ok_b2.aup2", work + L"out_b2.mp4", L"Test Output");
    const bool finished = start_and_pump(job_path, text);
    const Table table = results_of(job_path);
    check(finished, "busy host: run finishes");
    check(row_has_full_columns(table, 1) && table[1][1] == "FAILED" &&
              table[1][12] == "host did not return to edit state before the job",
          "busy host: first job FAILED with reason");
    check(row_has_full_columns(table, 2) && table[2][1] == "SKIPPED", "busy host: remaining job SKIPPED");
    check(g_opened.empty() && g_output_files.empty(), "busy host: no project or output calls");
}

void scenario_malformed_rejected(const std::wstring& work) {
    reset_fake(ExportMode::kAsync);
    const std::wstring job_path = work + L"jobs_malformed.txt";
    DeleteFileW(results_path_for(job_path).c_str());

    write_bytes(job_path, "ok_a.aup2\tout.mp4\n");
    SetEnvironmentVariableW(L"AUTO_EXPORT_JOBS", job_path.c_str());
    g_menu(nullptr);
    check(!g_run, "malformed: two-field line rejects the run");
    check(GetFileAttributesW(results_path_for(job_path).c_str()) == INVALID_FILE_ATTRIBUTES,
          "malformed: no results file is created");

    write_bytes(job_path, "ok_a.aup2\tout.mp4\tTest Output\nok_b.aup2\t\tTest Output\n");
    g_menu(nullptr);
    check(!g_run && g_opened.empty() && g_output_files.empty(),
          "malformed: empty field rejects the whole file before any job runs");
}

void scenario_utf8_bom_crlf(const std::wstring& work) {
    reset_fake(ExportMode::kAsync);
    const std::wstring job_path = work + L"jobs_utf8.txt";
    const std::wstring project = work + L"ok_日本.aup2";
    const std::wstring out = work + L"out_日本.mp4";
    const std::string text = "\xEF\xBB\xBF" + wide_to_utf8(project + L"\t" + out + L"\tTest Output") + "\r\n";
    const bool finished = start_and_pump(job_path, text);
    const Table table = results_of(job_path);
    check(finished, "utf8: run finishes");
    check(g_opened.size() == 1 && g_opened[0] == project, "utf8: Japanese project path passed through (BOM and CRLF)");
    check(row_has_full_columns(table, 1) && table[1][1] == "OK" && table[1][3] == wide_to_utf8(out),
          "utf8: Japanese output path written to results as UTF-8");
}

void scenario_default_job_file_path() {
    reset_fake(ExportMode::kAsync);
    const std::wstring dir = module_directory();
    const std::wstring job_path = dir + kDefaultJobFile;
    const std::wstring out = dir + L"out_default.mp4";
    DeleteFileW(results_path_for(job_path).c_str());

    SetEnvironmentVariableW(L"AUTO_EXPORT_JOBS", nullptr);  // unset: use the file next to the DLL
    write_bytes(job_path, job_line(dir + L"ok_default.aup2", out, L"Test Output"));
    g_menu(nullptr);
    const bool finished = pump_until_idle();
    const Table table = results_of(job_path);
    check(finished && row_has_full_columns(table, 1) && table[1][1] == "OK",
          "default path: auto_export_jobs.txt next to the DLL is used when the env var is unset");
    DeleteFileW(job_path.c_str());
    DeleteFileW(results_path_for(job_path).c_str());
    DeleteFileW(out.c_str());
}

}  // namespace

int main() {
    const std::wstring work = module_directory() + L"test_work\\";
    CreateDirectoryW(work.c_str(), nullptr);

    g_host.create_edit_handle = fake_create_edit_handle;
    g_host.register_project_load_handler = fake_register_project_load;
    g_host.register_event_listener = fake_register_event;
    g_host.register_edit_menu_param = fake_register_edit_menu;
    g_handle.get_edit_state = fake_get_edit_state;
    g_handle.get_edit_info = fake_get_edit_info;
    g_handle.open_project_file = fake_open_project_file;
    g_handle.output_file = fake_output_file;
    g_project.get_project_file_path = fake_project_path;

    RegisterPlugin(&g_host);
    check(g_menu != nullptr && g_load_handler != nullptr && g_listener != nullptr,
          "registration: menu, project load handler and CHANGE_EDIT_STATE listener");
    const COMMON_PLUGIN_TABLE* common = GetCommonPluginTable();
    check(common != nullptr && common->name != nullptr && common->information != nullptr,
          "registration: common plugin table");

    scenario_async_two_jobs(work);
    scenario_sync_export(work);
    scenario_open_failure_continues(work);
    scenario_output_refused(work);
    scenario_missing_output_file(work);
    scenario_load_not_observed(work);
    scenario_loaded_project_mismatch(work);
    scenario_export_start_not_observed(work);
    scenario_export_timeout(work);
    scenario_host_busy(work);
    scenario_malformed_rejected(work);
    scenario_utf8_bom_crlf(work);
    scenario_default_job_file_path();

    join_workers();
    UninitializePlugin();
    SetEnvironmentVariableW(L"AUTO_EXPORT_JOBS", nullptr);
    if (g_failures != 0) {
        std::printf("FAILED: %d check(s)\n", g_failures);
        return 1;
    }
    std::printf("ALL PASS\n");
    return 0;
}
