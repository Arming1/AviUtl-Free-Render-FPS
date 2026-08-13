// DEVELOPMENT / RECOVERY TEST ONLY.
// Loads the AviUtl2 output plugin ABI directly and exercises the project
// config save/load callbacks with an in-memory PROJECT_FILE implementation.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <cstdio>
#include <cstring>
#include <string>

#include "../../src/x264guiEx-FreeFPS/x264guiEx/output2.h"
#include "../../src/x264guiEx-FreeFPS/x264guiEx/project2.h"

static std::string g_key;
static std::string g_value;

static const char *get_param_string(const char *key) {
    return (key != nullptr && g_key == key) ? g_value.c_str() : nullptr;
}

static void set_param_string(const char *key, const char *value) {
    g_key = key ? key : "";
    g_value = value ? value : "";
}

static void clear_param() {
    g_key.clear();
    g_value.clear();
}

static bool get_param_binary(const char *, void *, int) { return false; }
static void set_param_binary(const char *, void *, int) {}
static const wchar_t *get_path() { return L"project_config_roundtrip.aup2"; }

int wmain(int argc, wchar_t **argv) {
    if (argc != 2) {
        std::fprintf(stderr, "usage: project_config_roundtrip <plugin.auo2>\n");
        return 2;
    }
    HMODULE module = LoadLibraryW(argv[1]);
    if (module == nullptr) {
        std::fprintf(stderr, "LoadLibrary failed: %lu\n", GetLastError());
        return 3;
    }
    using GetTable = OUTPUT_PLUGIN_TABLE *(__stdcall *)();
    auto get_table = reinterpret_cast<GetTable>(GetProcAddress(module, "GetOutputPluginTable"));
    if (get_table == nullptr) {
        std::fprintf(stderr, "GetOutputPluginTable missing\n");
        FreeLibrary(module);
        return 4;
    }
    OUTPUT_PLUGIN_TABLE *table = get_table();
    PROJECT_FILE project = {};
    project.get_param_string = get_param_string;
    project.set_param_string = set_param_string;
    project.clear_params = clear_param;
    project.get_param_binary = get_param_binary;
    project.set_param_binary = set_param_binary;
    project.get_project_file_path = get_path;

    bool save_ok = table && table->func_save_project_config
        && table->func_save_project_config(&project);
    bool shape_ok = g_key == "freerenderfps_config"
        && g_value.find("freefps_enable") != std::string::npos
        && g_value.find('\n') == std::string::npos;
    bool load_ok = table && table->func_load_project_config
        && table->func_load_project_config(&project);

    std::printf("save=%s load=%s key=%s bytes=%zu single_line=%s fields=%s\n",
        save_ok ? "PASS" : "FAIL", load_ok ? "PASS" : "FAIL", g_key.c_str(),
        g_value.size(), g_value.find('\n') == std::string::npos ? "PASS" : "FAIL",
        g_value.find("freefps_target_rate") != std::string::npos ? "PASS" : "FAIL");
    FreeLibrary(module);
    return (save_ok && shape_ok && load_ok) ? 0 : 1;
}
