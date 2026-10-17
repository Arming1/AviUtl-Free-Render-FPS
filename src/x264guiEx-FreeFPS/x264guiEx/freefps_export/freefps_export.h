#pragma once

// Free FPS Export (freefps_export.cpp) is part of the x64 build, x264guiEx-FreeRenderFPS.aux2. The functions below
// connect it to x264guiEx.cpp. The exports (GetCommonPluginTable, RegisterPlugin, UninitializePlugin) are defined in
// freefps_export.cpp.

#include <atomic>

struct LOG_HANDLE;
struct OUTPUT_PLUGIN_TABLE;

// True from just before output_file() of a Free FPS Export run until that run finishes or fails (set and cleared in
// freefps_export.cpp). The output callback (func_output in x264guiEx.cpp) reads it: the project was already re-timed
// by the menu, so the built-in FreeRenderFPS must not run for this export.
extern std::atomic<bool> g_freefps_menu_export_active;

// Receives the logger from InitializeLogger (x264guiEx.cpp). Logging is optional: without it nothing is written.
void freefps_export_set_logger(LOG_HANDLE* logger);

// Defined in x264guiEx.cpp. Runs the setup that GetOutputPluginTable does for AviUtl 1.x (settings, file filter,
// plugin name) and returns the output plugin table, which RegisterPlugin hands to the host.
OUTPUT_PLUGIN_TABLE* prepare_output_plugin_table();
