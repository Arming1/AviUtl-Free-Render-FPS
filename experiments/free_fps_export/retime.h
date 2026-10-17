// Pure re-timer for AviUtl2 .aup2 projects. No AviUtl dependency.
//
// C++ port of tools/aup2_fps_upconvert.py (target-fps and factor modes). The rules, whitelists and
// refusals are in tools/aup2_fps_upconvert.md. Output and sidecar JSON must match the Python tool
// byte-for-byte (tests/crosscheck.py).
#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace free_fps {

// Exact rate num/den in lowest terms, den > 0.
struct Rational {
    std::int64_t num = 0;
    std::int64_t den = 1;
};

// Parses "66" or "60000/1001" (ASCII digits only). Decimals, signs, spaces, zero and values beyond
// 64 bits are refused. On failure returns false and sets error (the text has no REFUSED prefix).
bool parse_target_rate(std::string_view text, Rational& out, std::string& error);

struct RetimeChange {
    std::size_t line = 0;                  // 1-based
    std::optional<std::string> old_text;   // empty optional for an inserted line (null in the sidecar)
    std::string new_text;
    bool inserted = false;
};

struct RetimeObjectTiming {
    std::size_t line = 0;                  // line of the object's frame= (1-based)
    std::string start_error_s;             // exact n/d (or n) string, as the Python tool writes it
    std::string end_error_s;
    bool shortened = false;
};

struct RetimeResult {
    bool ok = false;
    std::size_t refusal_line = 0;          // 1-based; 0 when the refusal is not about one line
    std::string refusal_reason;

    std::string output;                    // retimed .aup2 bytes (BOM and line endings kept)
    std::string input_sha256;
    std::string output_sha256;
    std::optional<std::int64_t> factor;    // K in factor mode, empty in target mode (JSON null)
    std::string source_rate;               // "30/1"
    std::string target_rate;               // "66/1", "60000/1001"
    std::string k;                         // "11/5"
    std::vector<RetimeChange> changes;
    std::vector<std::string> notes;
    std::string max_start_error_s;
    std::string max_end_error_s;
    std::vector<RetimeObjectTiming> objects;
};

// "REFUSED: line N: reason", or "REFUSED: reason" when no line applies (same text as the Python tool).
std::string refusal_message(const RetimeResult& result);

// Re-times in_utf8 (the complete .aup2 file bytes, optional UTF-8 BOM) to the exact target rate.
// out_path is written into the [project] file= line, so pass the absolute output path (the Python
// tool writes os.path.abspath(--out)). Never touches the file system.
RetimeResult retime(std::string_view in_utf8, Rational target, std::wstring out_path);

// Factor mode: target = scene rate * factor, video.scale unchanged. factor must be >= 2.
RetimeResult retime_factor(std::string_view in_utf8, std::int64_t factor, std::wstring out_path);

// Sidecar JSON with the layout of the Python tool's <out>.convert.json (indent 2, LF, UTF-8).
// in_path and out_path must be absolute paths, as the Python tool records them.
std::string sidecar_json(const RetimeResult& result, const std::wstring& in_path, const std::wstring& out_path);

std::string wide_to_utf8(const std::wstring& text);
bool utf8_to_wide(std::string_view text, std::wstring& out);

}  // namespace free_fps
