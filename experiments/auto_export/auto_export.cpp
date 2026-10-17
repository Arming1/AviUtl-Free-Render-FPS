// Auto export job runner: an AviUtl2 general plugin (.aux2) that runs a list of
// export jobs through the public SDK (plugin2.h / logger2.h). Used for automated
// regression testing of output plugins.
//
// Menu: Edit > Auto Export > Run Job List
//
// Job file: the path in the AUTO_EXPORT_JOBS environment variable, otherwise
// auto_export_jobs.txt next to the .aux2. UTF-8, one job per line:
//     project<TAB>output<TAB>output_plugin
// Blank lines and lines starting with '#' are ignored. The whole file is rejected
// if any other line does not have exactly three non-empty tab-separated fields.
//
// Results: <job file>.results.tsv, rewritten at the start of every run.
//
// Threading: every SDK call is made on the host main thread. The menu callback
// starts a thread timer (SetTimer with no window) and each tick advances the job
// state machine by one step. The CHANGE_EDIT_STATE listener runs on the
// notification thread; it only calls get_edit_state() and sets an atomic flag,
// which the SDK allows (only call_edit_section() is forbidden there). The project
// load handler only stores the loaded path.
//
// Limits: the SDK reports the end of an export only as the edit state returning
// to EDIT. The job is therefore judged by the output file existing with a
// non-zero size, not by the output plugin's own return value.

#include <windows.h>

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "logger2.h"
#include "plugin2.h"

#pragma comment(lib, "user32.lib")  // SetTimer / KillTimer

namespace {

// Time limits in milliseconds. The host-free test (tests/host_stub_test.cpp)
// overrides these to keep runs short.
struct Timing {
    DWORD tick_ms = 100;
    ULONGLONG ready_timeout_ms = 60000;         // edit state must return to EDIT before a job
    ULONGLONG load_timeout_ms = 30000;          // project load callback after open_project_file
    ULONGLONG export_start_timeout_ms = 30000;  // CHANGE_EDIT_STATE(SAVE) after output_file
    ULONGLONG export_timeout_ms = 3600000;      // whole export
};

Timing g_timing;

constexpr UINT_PTR kTimerId = 0x4145;
constexpr wchar_t kJobFileEnv[] = L"AUTO_EXPORT_JOBS";
constexpr wchar_t kDefaultJobFile[] = L"auto_export_jobs.txt";
constexpr wchar_t kResultsHeader[] =
    L"index\tstatus\tproject\toutput\tplugin\trate\tscale\tframe_max\twidth\theight"
    L"\toutput_bytes\telapsed_ms\treason\n";
constexpr LONGLONG kMaxJobFileBytes = 1024 * 1024;

constexpr int kStateEdit = EDIT_HANDLE::EDIT_STATE_EDIT;
constexpr int kStateSave = EDIT_HANDLE::EDIT_STATE_SAVE;

LOG_HANDLE* g_logger = nullptr;
EDIT_HANDLE* g_edit = nullptr;

// Set by the CHANGE_EDIT_STATE listener (notification thread) or by polling in
// the job state machine (main thread). Cleared before each output_file call.
std::atomic<bool> g_save_seen{false};
// Incremented by the project load handler.
std::atomic<unsigned> g_load_seq{0};
std::mutex g_load_mutex;
std::wstring g_loaded_path;  // guarded by g_load_mutex

enum class Level { kInfo, kWarn, kError };

void log(Level level, const std::wstring& message) {
    if (g_logger == nullptr) {
        return;
    }
    const std::wstring text = L"[AutoExport] " + message;
    switch (level) {
    case Level::kInfo:
        g_logger->info(g_logger, text.c_str());
        break;
    case Level::kWarn:
        g_logger->warn(g_logger, text.c_str());
        break;
    case Level::kError:
        g_logger->error(g_logger, text.c_str());
        break;
    }
}

bool utf8_to_wide(const std::string& text, std::wstring& out) {
    out.clear();
    if (text.empty()) {
        return true;
    }
    const int length = static_cast<int>(text.size());
    const int size = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), length, nullptr, 0);
    if (size <= 0) {
        return false;
    }
    out.resize(static_cast<size_t>(size));
    return MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), length, &out[0], size) == size;
}

std::string wide_to_utf8(const std::wstring& text) {
    if (text.empty()) {
        return std::string();
    }
    const int length = static_cast<int>(text.size());
    const int size = WideCharToMultiByte(CP_UTF8, 0, text.data(), length, nullptr, 0, nullptr, nullptr);
    if (size <= 0) {
        return std::string();
    }
    std::string out(static_cast<size_t>(size), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.data(), length, &out[0], size, nullptr, nullptr);
    return out;
}

bool read_file_bytes(const std::wstring& path, std::string& bytes) {
    bytes.clear();
    HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                              nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        return false;
    }
    LARGE_INTEGER size{};
    bool ok = GetFileSizeEx(file, &size) && size.QuadPart <= kMaxJobFileBytes;
    if (ok && size.QuadPart > 0) {
        bytes.resize(static_cast<size_t>(size.QuadPart));
        DWORD read = 0;
        ok = ReadFile(file, &bytes[0], static_cast<DWORD>(bytes.size()), &read, nullptr) &&
             read == bytes.size();
    }
    CloseHandle(file);
    return ok;
}

// Returns the size of the file, or -1 if it cannot be read.
long long file_size(const std::wstring& path) {
    WIN32_FILE_ATTRIBUTE_DATA data{};
    if (!GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &data)) {
        return -1;
    }
    LARGE_INTEGER size{};
    size.HighPart = static_cast<LONG>(data.nFileSizeHigh);
    size.LowPart = data.nFileSizeLow;
    return size.QuadPart;
}

// Directory of this DLL, with a trailing backslash.
std::wstring module_directory() {
    static const int anchor = 0;
    HMODULE module = nullptr;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                                GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                            reinterpret_cast<LPCWSTR>(&anchor), &module)) {
        return std::wstring();
    }
    wchar_t path[MAX_PATH * 4];
    const DWORD length = GetModuleFileNameW(module, path, static_cast<DWORD>(sizeof(path) / sizeof(path[0])));
    if (length == 0 || length >= sizeof(path) / sizeof(path[0])) {
        return std::wstring();
    }
    const std::wstring full(path, length);
    const size_t slash = full.find_last_of(L"\\/");
    return slash == std::wstring::npos ? std::wstring() : full.substr(0, slash + 1);
}

std::wstring resolve_job_file() {
    std::vector<wchar_t> buffer(32768);
    const DWORD length = GetEnvironmentVariableW(kJobFileEnv, buffer.data(), static_cast<DWORD>(buffer.size()));
    if (length > 0 && length < buffer.size()) {
        return std::wstring(buffer.data(), length);
    }
    return module_directory() + kDefaultJobFile;
}

std::wstring full_path(const std::wstring& path) {
    const DWORD needed = GetFullPathNameW(path.c_str(), 0, nullptr, nullptr);
    if (needed == 0) {
        return path;
    }
    std::vector<wchar_t> buffer(needed);
    const DWORD length = GetFullPathNameW(path.c_str(), needed, buffer.data(), nullptr);
    if (length == 0 || length >= needed) {
        return path;
    }
    return std::wstring(buffer.data(), length);
}

bool same_path(const std::wstring& a, const std::wstring& b) {
    const std::wstring fa = full_path(a);
    const std::wstring fb = full_path(b);
    return CompareStringOrdinal(fa.c_str(), static_cast<int>(fa.size()), fb.c_str(),
                                static_cast<int>(fb.size()), TRUE) == CSTR_EQUAL;
}

struct Job {
    std::wstring project;
    std::wstring output;
    std::wstring plugin;
};

bool parse_job_file(const std::string& bytes, std::vector<Job>& jobs, std::wstring& error) {
    jobs.clear();
    size_t pos = 0;
    if (bytes.size() >= 3 && bytes.compare(0, 3, "\xEF\xBB\xBF") == 0) {
        pos = 3;  // UTF-8 BOM
    }
    unsigned line_number = 0;
    while (pos < bytes.size()) {
        size_t end = bytes.find('\n', pos);
        if (end == std::string::npos) {
            end = bytes.size();
        }
        std::string line = bytes.substr(pos, end - pos);
        pos = end + 1;
        ++line_number;
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        if (line.empty() || line[0] == '#') {
            continue;
        }

        const std::wstring where = L"line " + std::to_wstring(line_number) + L": ";
        std::vector<std::string> fields;
        size_t field_start = 0;
        for (;;) {
            const size_t tab = line.find('\t', field_start);
            fields.push_back(line.substr(field_start, tab == std::string::npos ? std::string::npos : tab - field_start));
            if (tab == std::string::npos) {
                break;
            }
            field_start = tab + 1;
        }
        if (fields.size() != 3) {
            error = where + L"expected 3 tab-separated fields";
            return false;
        }
        Job job;
        if (!utf8_to_wide(fields[0], job.project) || !utf8_to_wide(fields[1], job.output) ||
            !utf8_to_wide(fields[2], job.plugin)) {
            error = where + L"invalid UTF-8";
            return false;
        }
        if (job.project.empty() || job.output.empty() || job.plugin.empty()) {
            error = where + L"empty field";
            return false;
        }
        jobs.push_back(std::move(job));
    }
    return true;
}

enum class Status { kOk, kFailed, kSkipped };

const wchar_t* status_name(Status status) {
    switch (status) {
    case Status::kOk:
        return L"OK";
    case Status::kFailed:
        return L"FAILED";
    case Status::kSkipped:
        return L"SKIPPED";
    }
    return L"?";
}

// Scene values read with get_edit_info() after the job's project was loaded.
// frame_max is EDIT_INFO::frame_max (last frame holding an object), not the
// rendered frame count.
struct SceneInfo {
    bool valid = false;
    int rate = 0;
    int scale = 0;
    int frame_max = 0;
    int width = 0;
    int height = 0;
};

enum class Phase { kWaitReady, kOpen, kWaitLoad, kStart, kWaitExport };

struct Run {
    std::vector<Job> jobs;
    HANDLE results = INVALID_HANDLE_VALUE;
    size_t index = 0;
    Phase phase = Phase::kWaitReady;
    ULONGLONG job_started = 0;
    ULONGLONG phase_started = 0;
    unsigned load_seq_before = 0;
    SceneInfo scene;
    size_t ok = 0;
    size_t failed = 0;
    size_t skipped = 0;
    bool done = false;

    Run() = default;
    Run(const Run&) = delete;
    Run& operator=(const Run&) = delete;
    ~Run() {
        if (results != INVALID_HANDLE_VALUE) {
            CloseHandle(results);
        }
    }
};

std::unique_ptr<Run> g_run;

void write_text(HANDLE file, const std::wstring& text) {
    const std::string bytes = wide_to_utf8(text);
    DWORD written = 0;
    WriteFile(file, bytes.data(), static_cast<DWORD>(bytes.size()), &written, nullptr);
}

std::wstring number_field(bool valid, long long value) {
    return valid ? std::to_wstring(value) : L"-";
}

void write_row(HANDLE file, size_t number, const Job& job, Status status, const SceneInfo& scene,
               long long output_bytes, ULONGLONG elapsed_ms, const std::wstring& reason) {
    std::wstring row = std::to_wstring(number) + L"\t" + status_name(status) + L"\t" + job.project +
                       L"\t" + job.output + L"\t" + job.plugin + L"\t" +
                       number_field(scene.valid, scene.rate) + L"\t" +
                       number_field(scene.valid, scene.scale) + L"\t" +
                       number_field(scene.valid, scene.frame_max) + L"\t" +
                       number_field(scene.valid, scene.width) + L"\t" +
                       number_field(scene.valid, scene.height) + L"\t" +
                       number_field(output_bytes >= 0, output_bytes) + L"\t" +
                       std::to_wstring(elapsed_ms) + L"\t" + reason + L"\n";
    write_text(file, row);
}

void begin_job(Run& run) {
    const ULONGLONG now = GetTickCount64();
    run.scene = SceneInfo();
    run.phase = Phase::kWaitReady;
    run.phase_started = now;
    run.job_started = now;
    log(Level::kInfo, L"job " + std::to_wstring(run.index + 1) + L"/" + std::to_wstring(run.jobs.size()) +
                          L": " + run.jobs[run.index].project);
}

void complete_job(Run& run, Status status, const std::wstring& reason, long long output_bytes) {
    const Job& job = run.jobs[run.index];
    const ULONGLONG elapsed = GetTickCount64() - run.job_started;
    write_row(run.results, run.index + 1, job, status, run.scene, output_bytes, elapsed, reason);
    if (status == Status::kOk) {
        ++run.ok;
        log(Level::kInfo, L"job " + std::to_wstring(run.index + 1) + L" OK: " + job.output);
    } else {
        ++run.failed;
        log(Level::kError, L"job " + std::to_wstring(run.index + 1) + L" FAILED: " + reason);
    }
    ++run.index;
}

void finish_or_begin(Run& run) {
    if (run.index >= run.jobs.size()) {
        run.done = true;
    } else {
        begin_job(run);
    }
}

void skip_rest(Run& run, const std::wstring& reason) {
    for (; run.index < run.jobs.size(); ++run.index) {
        write_row(run.results, run.index + 1, run.jobs[run.index], Status::kSkipped, SceneInfo(), -1, 0, reason);
        ++run.skipped;
    }
}

// Records the current job as FAILED, marks the remaining jobs SKIPPED and ends
// the run. Used when the host may still be busy, so later jobs cannot start.
void abort_run(Run& run, const std::wstring& reason) {
    complete_job(run, Status::kFailed, reason, -1);
    skip_rest(run, L"run aborted: " + reason);
    run.done = true;
}

void read_scene(Run& run) {
    EDIT_INFO info = {};
    g_edit->get_edit_info(&info, sizeof(info));
    run.scene.valid = true;
    run.scene.rate = info.rate;
    run.scene.scale = info.scale;
    run.scene.frame_max = info.frame_max;
    run.scene.width = info.width;
    run.scene.height = info.height;
}

// One state-machine step. Runs on the host main thread from the timer.
void tick(Run& run) {
    const ULONGLONG now = GetTickCount64();
    const ULONGLONG waited = now - run.phase_started;
    const Job& job = run.jobs[run.index];
    const int state = g_edit->get_edit_state();

    switch (run.phase) {
    case Phase::kWaitReady:
        if (state == kStateEdit) {
            run.phase = Phase::kOpen;
        } else if (waited > g_timing.ready_timeout_ms) {
            abort_run(run, L"host did not return to edit state before the job");
        }
        return;

    case Phase::kOpen:
        run.load_seq_before = g_load_seq.load();
        if (!g_edit->open_project_file(job.project.c_str(), false)) {
            complete_job(run, Status::kFailed, L"open_project_file returned false", -1);
            finish_or_begin(run);
            return;
        }
        run.phase = Phase::kWaitLoad;
        run.phase_started = now;
        return;

    case Phase::kWaitLoad:
        if (g_load_seq.load() != run.load_seq_before) {
            std::wstring loaded;
            {
                std::lock_guard<std::mutex> lock(g_load_mutex);
                loaded = g_loaded_path;
            }
            if (!same_path(loaded, job.project)) {
                complete_job(run, Status::kFailed, L"loaded project differs: " + loaded, -1);
                finish_or_begin(run);
                return;
            }
            read_scene(run);
            run.phase = Phase::kStart;
            run.phase_started = now;
        } else if (waited > g_timing.load_timeout_ms) {
            complete_job(run, Status::kFailed, L"project load callback not observed", -1);
            finish_or_begin(run);
        }
        return;

    case Phase::kStart:
        g_save_seen = false;
        if (!g_edit->output_file(job.output.c_str(), job.plugin.c_str(), nullptr, nullptr)) {
            complete_job(run, Status::kFailed, L"output_file returned false", -1);
            finish_or_begin(run);
            return;
        }
        run.phase = Phase::kWaitExport;
        run.phase_started = now;
        return;

    case Phase::kWaitExport:
        if (state == kStateSave) {
            g_save_seen = true;
        }
        if (g_save_seen && state == kStateEdit) {
            const long long bytes = file_size(job.output);
            if (bytes > 0) {
                complete_job(run, Status::kOk, L"", bytes);
            } else {
                complete_job(run, Status::kFailed, L"output file missing or empty after export", bytes);
            }
            finish_or_begin(run);
        } else if (!g_save_seen && waited > g_timing.export_start_timeout_ms) {
            abort_run(run, L"export start (CHANGE_EDIT_STATE) not observed");
        } else if (waited > g_timing.export_timeout_ms) {
            abort_run(run, L"export timeout");
        }
        return;
    }
}

void finish_run() {
    KillTimer(nullptr, kTimerId);
    const Run& run = *g_run;
    log(Level::kInfo, L"run finished: ok=" + std::to_wstring(run.ok) + L" failed=" +
                          std::to_wstring(run.failed) + L" skipped=" + std::to_wstring(run.skipped));
    g_run.reset();
}

void CALLBACK on_timer(HWND, UINT, UINT_PTR, DWORD) {
    if (!g_run) {
        KillTimer(nullptr, kTimerId);
        return;
    }
    tick(*g_run);
    if (g_run->done) {
        finish_run();
    }
}

void on_menu(void*) {
    if (g_run) {
        log(Level::kWarn, L"a job run is already active");
        return;
    }
    if (g_edit == nullptr) {
        return;
    }

    const std::wstring job_file = resolve_job_file();
    std::string bytes;
    if (!read_file_bytes(job_file, bytes)) {
        log(Level::kError, L"cannot read job file: " + job_file);
        return;
    }
    std::vector<Job> jobs;
    std::wstring error;
    if (!parse_job_file(bytes, jobs, error)) {
        log(Level::kError, L"rejected job file " + job_file + L": " + error);
        return;
    }
    if (jobs.empty()) {
        log(Level::kWarn, L"job file has no jobs: " + job_file);
        return;
    }

    const std::wstring results_path = job_file + L".results.tsv";
    HANDLE results = CreateFileW(results_path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                                 FILE_ATTRIBUTE_NORMAL, nullptr);
    if (results == INVALID_HANDLE_VALUE) {
        log(Level::kError, L"cannot create results file: " + results_path);
        return;
    }
    auto run = std::make_unique<Run>();
    run->jobs = std::move(jobs);
    run->results = results;
    write_text(results, kResultsHeader);

    if (SetTimer(nullptr, kTimerId, g_timing.tick_ms, on_timer) == 0) {
        log(Level::kError, L"cannot start the job timer");
        return;
    }
    log(Level::kInfo, L"started " + std::to_wstring(run->jobs.size()) + L" job(s) from " + job_file);
    g_run = std::move(run);
    begin_job(*g_run);
}

void on_edit_state_event(void*) {
    if (g_edit != nullptr && g_edit->get_edit_state() == kStateSave) {
        g_save_seen = true;
    }
}

void on_project_load(PROJECT_FILE* project) {
    std::wstring path;
    if (project != nullptr && project->get_project_file_path != nullptr) {
        const LPCWSTR current = project->get_project_file_path();
        if (current != nullptr) {
            path = current;
        }
    }
    {
        std::lock_guard<std::mutex> lock(g_load_mutex);
        g_loaded_path = path;
    }
    g_load_seq.fetch_add(1);
}

}  // namespace

extern "C" {

__declspec(dllexport) COMMON_PLUGIN_TABLE* GetCommonPluginTable() {
    static COMMON_PLUGIN_TABLE table = {
        L"Auto Export (regression jobs)",
        L"Runs a list of export jobs through the public SDK for regression testing.",
    };
    return &table;
}

__declspec(dllexport) void InitializeLogger(LOG_HANDLE* logger) {
    g_logger = logger;
}

__declspec(dllexport) void RegisterPlugin(HOST_APP_TABLE* host) {
    g_edit = host->create_edit_handle();
    host->register_project_load_handler(on_project_load);
    host->register_event_listener(EVENT_TYPE::CHANGE_EDIT_STATE, nullptr, on_edit_state_event);
    host->register_edit_menu_param(L"Auto Export\\Run Job List", nullptr, on_menu);
}

__declspec(dllexport) void UninitializePlugin() {
    if (g_run) {
        KillTimer(nullptr, kTimerId);
        g_run.reset();
    }
}

}  // extern "C"
