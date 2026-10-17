// DEVELOPMENT / RECOVERY TEST ONLY.
// Loads the legacy AviUtl output plugin ABI and exercises the configuration
// callbacks that AviUtl calls before opening the output settings dialog.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <cstdio>
#include <vector>

#include "../../src/x264guiEx-FreeFPS/x264guiEx/output.h"

int wmain(int argc, wchar_t **argv) {
    if (argc != 2) {
        std::fwprintf(stderr, L"usage: legacy_config_probe <plugin.auo>\n");
        return 2;
    }

    HMODULE module = LoadLibraryW(argv[1]);
    if (module == nullptr) {
        std::fprintf(stderr, "LoadLibrary failed: %lu\n", GetLastError());
        return 3;
    }

    using GetTable = OUTPUT_PLUGIN_TABLE *(__stdcall *)();
    auto get_table = reinterpret_cast<GetTable>(
        GetProcAddress(module, "GetOutputPluginTable"));
    if (get_table == nullptr) {
        std::fprintf(stderr, "GetOutputPluginTable missing\n");
        FreeLibrary(module);
        return 4;
    }

    OUTPUT_PLUGIN_TABLE *table = get_table();
    if (table == nullptr || table->func_config_get == nullptr) {
        std::fprintf(stderr, "output table or func_config_get missing\n");
        FreeLibrary(module);
        return 5;
    }

    const int required = table->func_config_get(nullptr, 0);
    if (required <= 0) {
        std::fprintf(stderr, "func_config_get(size) returned %d\n", required);
        FreeLibrary(module);
        return 6;
    }

    std::vector<unsigned char> config(static_cast<size_t>(required), 0);
    const int written = table->func_config_get(config.data(), required);
    const bool ok = written == required;
    std::printf("plugin=%s config_size=%d written=%d result=%s\n",
        table->name ? table->name : "(null)", required, written,
        ok ? "PASS" : "FAIL");

    FreeLibrary(module);
    return ok ? 0 : 1;
}
