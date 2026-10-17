#pragma once

// Free FPS Export (freefps_export.cpp) is part of the x64 build, x264guiEx-FreeRenderFPS.aux2. The functions below
// connect it to x264guiEx.cpp. The exports (GetCommonPluginTable, RegisterPlugin, UninitializePlugin) are defined in
// freefps_export.cpp.

#include <atomic>
#include <cstddef>

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

// Encoder settings for the Free FPS Export dialog. All of them use the output settings (g_conf), the same ones func_output
// encodes with. Defined in x264guiEx.cpp.
// Opens the x264guiEx settings window (ShowfrmConfig, as the output settings do). Edits go straight into g_conf.
// Returns false if the settings are not available.
bool freefps_open_encoder_settings();
// One-line summary, e.g. "CRF 23 / medium / AAC 256kbps / mp4". Written to buf (count wide characters, NUL-terminated).
// The container is the extension of output_path when the encoder recognises it, else the default container.
// output_path may be nullptr (default container).
void freefps_get_encoder_summary(wchar_t* buf, std::size_t count, const wchar_t* output_path);
// Default output container name without the dot, e.g. "mp4" (from default_output_ext).
void freefps_get_container_name(wchar_t* buf, std::size_t count);
// True if the encoder takes the extension of path as a container (.mp4, .mkv, or raw). Otherwise it appends the default
// container extension when it writes the output.
bool freefps_has_known_container_ext(const wchar_t* path);
