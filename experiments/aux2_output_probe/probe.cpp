// Minimal AviUtl2 probe: can a general plugin (.aux2) register an OUTPUT plugin
// through HOST_APP_TABLE::register_output_plugin, and can it then be selected by
// EDIT_HANDLE::output_file()? Test-only; not a production plugin.
//
// Registers:
//   - output plugin "FreeFPS probe output" (video + audio, *.txt). Its func_output
//     writes w/h/rate/scale/n/audio_rate/audio_n/flag and whether
//     func_get_video(0, 0) returned a pointer.
//   - Edit > FreeFPS probe > Run probe export: calls output_file() on that plugin
//     with %TEMP%\freefps_probe_out.txt and shows the return value.
//
// Logging goes through InitializeLogger when the host provides it.

#include <windows.h>

#include <cstdio>
#include <string>

#include "logger2.h"
#include "plugin2.h"
#include "output2.h"

#pragma comment(lib, "user32.lib")  // MessageBoxW

namespace {

constexpr wchar_t kOutputName[] = L"FreeFPS probe output";
constexpr wchar_t kMenuName[] = L"FreeFPS probe\\Run probe export";
constexpr wchar_t kProbeFileName[] = L"freefps_probe_out.txt";
constexpr wchar_t kTitle[] = L"FreeFPS aux2 output probe";

LOG_HANDLE* g_logger = nullptr;
EDIT_HANDLE* g_edit = nullptr;

void log_info(const wchar_t* message) {
    if (g_logger != nullptr) {
        g_logger->info(g_logger, message);
    }
}

void log_error(const wchar_t* message) {
    if (g_logger != nullptr) {
        g_logger->error(g_logger, message);
    }
}

// Output callback. Called by the host on its export thread.
bool probe_output(OUTPUT_INFO* oip) {
    log_info(L"probe func_output: enter");
    if (oip == nullptr || oip->savefile == nullptr) {
        log_error(L"probe func_output: null OUTPUT_INFO or savefile");
        return false;
    }

    // Only valid during func_output; the pointer is not dereferenced here.
    const void* video0 = oip->func_get_video != nullptr ? oip->func_get_video(0, 0) : nullptr;

    char text[512] = {};
    const int length = std::snprintf(
        text, sizeof(text),
        "w=%d\nh=%d\nrate=%d\nscale=%d\nn=%d\naudio_rate=%d\naudio_n=%d\nflag=%d\nvideo0_ok=%d\n",
        oip->w, oip->h, oip->rate, oip->scale, oip->n,
        oip->audio_rate, oip->audio_n, oip->flag, video0 != nullptr ? 1 : 0);
    if (length <= 0 || static_cast<size_t>(length) >= sizeof(text)) {
        log_error(L"probe func_output: text buffer too small");
        return false;
    }

    FILE* file = nullptr;
    if (_wfopen_s(&file, oip->savefile, L"wb") != 0 || file == nullptr) {
        log_error(L"probe func_output: could not open savefile");
        return false;
    }
    const size_t written = std::fwrite(text, 1, static_cast<size_t>(length), file);
    std::fclose(file);
    if (written != static_cast<size_t>(length)) {
        log_error(L"probe func_output: short write");
        return false;
    }

    log_info(L"probe func_output: wrote savefile");
    return true;
}

// Config callback. Shows dll_hinst so the 2026/6/20 dll_hinst fix can be checked.
bool probe_config(HWND hwnd, HINSTANCE dll_hinst) {
    wchar_t message[128] = {};
    std::swprintf(message, sizeof(message) / sizeof(message[0]),
                  L"probe config: dll_hinst=%p", static_cast<void*>(dll_hinst));
    log_info(message);
    MessageBoxW(hwnd, message, kTitle, MB_OK);
    return true;
}

// Edit menu callback. Called on the host main thread without an edit section.
void on_probe_menu(void* /*param*/) {
    log_info(L"probe menu: enter");
    if (g_edit == nullptr) {
        log_error(L"probe menu: no EDIT_HANDLE");
        MessageBoxW(nullptr, L"create_edit_handle() returned nullptr", kTitle, MB_OK);
        return;
    }

    wchar_t temp_dir[MAX_PATH] = {};
    const DWORD temp_length = GetTempPathW(MAX_PATH, temp_dir);
    if (temp_length == 0 || temp_length >= MAX_PATH) {
        log_error(L"probe menu: GetTempPathW failed");
        MessageBoxW(nullptr, L"GetTempPathW failed", kTitle, MB_OK);
        return;
    }
    const std::wstring output_path = std::wstring(temp_dir) + kProbeFileName;

    const bool ok = g_edit->output_file(output_path.c_str(), kOutputName, nullptr, nullptr);
    log_info(ok ? L"probe menu: output_file returned true" : L"probe menu: output_file returned false");

    std::wstring message = L"output_file returned ";
    message += ok ? L"true" : L"false";
    message += L"\n";
    message += output_path;
    MessageBoxW(nullptr, message.c_str(), kTitle, MB_OK);
}

OUTPUT_PLUGIN_TABLE g_output_table = {
    OUTPUT_PLUGIN_TABLE::FLAG_VIDEO | OUTPUT_PLUGIN_TABLE::FLAG_AUDIO,
    kOutputName,
    L"Probe text (*.txt)\0*.txt\0\0",
    L"Test-only output plugin: writes frame and audio counters as text.",
    probe_output,
    probe_config,
    nullptr,  // func_get_config_text
    nullptr,  // func_load_project_config
    nullptr,  // func_save_project_config
};

}  // namespace

extern "C" {

__declspec(dllexport) COMMON_PLUGIN_TABLE* GetCommonPluginTable() {
    static COMMON_PLUGIN_TABLE table = {
        L"FreeFPS aux2 output probe",
        L"Test-only probe: registers an output plugin from a general .aux2.",
    };
    return &table;
}

__declspec(dllexport) void InitializeLogger(LOG_HANDLE* logger) {
    g_logger = logger;
    log_info(L"probe InitializeLogger: logger received");
}

__declspec(dllexport) void RegisterPlugin(HOST_APP_TABLE* host) {
    log_info(L"probe RegisterPlugin: enter");
    if (host == nullptr) {
        log_error(L"probe RegisterPlugin: null HOST_APP_TABLE");
        return;
    }

    host->register_output_plugin(&g_output_table);
    log_info(L"probe RegisterPlugin: register_output_plugin called");

    g_edit = host->create_edit_handle();
    log_info(g_edit != nullptr ? L"probe RegisterPlugin: EDIT_HANDLE acquired"
                               : L"probe RegisterPlugin: create_edit_handle returned nullptr");

    host->register_edit_menu_param(kMenuName, nullptr, on_probe_menu);
    log_info(L"probe RegisterPlugin: edit menu registered");
}

}  // extern "C"
