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

struct PreciseSkip {
    std::size_t line = 0;                  // line of the object's frame= (1-based)
    std::string reason;
};

struct PreciseTrack {
    std::size_t object_line = 0;           // line of the object's frame= (1-based)
    std::size_t line = 0;                  // line of the trackbar row (1-based)
    std::string max_quant_error;           // 6 decimals
    std::string max_quant_error_pct;       // percent of the range (max - min of the old values), 6 decimals
    std::string end_extrapolation;         // new last value - old last value, 6 decimals (0 when the value is held at the
                                           // original end value; field name kept for the sidecar)
};

struct PreciseReport {
    std::size_t extra_decimals = 0;        // written to the sidecar only when > 0
    std::size_t applied = 0;               // objects with at least one trackbar, all of them re-sampled
    std::vector<PreciseSkip> skipped;
    std::vector<PreciseTrack> tracks;
};

// How scenes other than the exported one (display.scene) follow the target rate.
//   Auto:   a scene whose own rate is >= the exported scene's rate follows; a slower one is kept as it is
//           (treated as deliberate frame dropping).
//   Follow: every scene follows.
//   Keep:   only the exported scene follows; every other scene is left byte-for-byte unchanged.
// The exported scene always follows. A followed scene is re-timed by k = target rate / its own rate.
enum class SubsceneRatePolicy { Auto, Follow, Keep };

// One scene of the project. RetimeResult::scenes lists them by scene number.
struct ScenePlan {
    int scene = 0;
    std::string name;                      // name= of the scene section, empty when absent
    std::string source_rate;               // "60/1"
    std::string target_rate;               // "60/1"; equals source_rate for a kept scene
    std::string k;                         // "1/1" for a kept scene
    bool follow = true;                    // false: the scene is kept at its own rate
    bool exported = false;                 // the scene named by display.scene
};

struct RetimeResult {
    bool ok = false;
    std::size_t refusal_line = 0;          // 1-based; 0 when the refusal is not about one line
    std::string refusal_reason;

    std::string output;                    // retimed .aup2 bytes (BOM and line endings kept)
    std::string input_sha256;
    std::string output_sha256;
    std::optional<std::int64_t> factor;    // K in factor mode, empty in target mode (JSON null)
    std::string source_rate;               // "30/1"; rate of the exported scene (display.scene)
    std::string target_rate;               // "66/1", "60000/1001"
    std::string k;                         // "11/5"; target rate / source_rate
    std::string subscene_rate;             // "auto", "follow" or "keep": the policy that was applied
    std::vector<ScenePlan> scenes;         // every scene, by scene number; filled on success, and on a refusal
                                           // that comes after the scenes were read (empty otherwise)
    std::vector<RetimeChange> changes;
    std::vector<std::string> notes;
    std::string max_start_error_s;
    std::string max_end_error_s;
    std::vector<RetimeObjectTiming> objects;
    std::optional<PreciseReport> precise;  // set only in precise mode; the sidecar has no "precise" key otherwise
};

// "REFUSED: line N: reason", or "REFUSED: reason" when no line applies (same text as the Python tool).
std::string refusal_message(const RetimeResult& result);

// Re-times in_utf8 (the complete .aup2 file bytes, optional UTF-8 BOM) to the exact target rate.
// out_path is written into the [project] file= line, so pass the absolute output path (the Python
// tool writes os.path.abspath(--out)). Never touches the file system.
// precise: also re-samples the trackbars of each eligible object at the new keyframes, exactly (see PreciseReport).
// Each new frame shows the original value at the same moment, with the original curve held at its end values (an object
// keeps its last value after its last frame): keyframes are added where the hold starts, so an object's frame= row can
// have more points than the rescaled one. An object with a 時間制御(オブジェクト) effect is left alone.
// Off by default, and then the output is the same as without the arguments.
// The 位置 of a 時間制御(オブジェクト) is always rewritten (also without precise): a straight 直線移動(時間制御) line when
// that is exact, otherwise a 瞬間移動 staircase (at most 4096 points) with コマ落ち 1; a scene at k = 1 is not touched.
// precise_extra_decimals: precise mode only; each re-sampled row gets this many decimals more than its own
// largest decimal count (0 keeps the row's own count).
// subscene_rate: which scenes follow the target rate (see SubsceneRatePolicy). The exported scene is the one
// named by [project] display.scene (0 when absent); a display.scene that has no [scene.N] is refused. Scenes may
// have different rates. With one scene, or all scenes at one rate under Auto, the output is the same as before
// this option existed (only the sidecar has the new keys).
RetimeResult retime(std::string_view in_utf8, Rational target, std::wstring out_path, bool precise = false,
                    std::size_t precise_extra_decimals = 0,
                    SubsceneRatePolicy subscene_rate = SubsceneRatePolicy::Auto);

// Factor mode: target = rate of the exported scene * factor, video.scale of that scene unchanged. factor must be >= 2.
RetimeResult retime_factor(std::string_view in_utf8, std::int64_t factor, std::wstring out_path,
                           bool precise = false, std::size_t precise_extra_decimals = 0,
                           SubsceneRatePolicy subscene_rate = SubsceneRatePolicy::Auto);

// Sidecar JSON with the layout of the Python tool's <out>.convert.json (indent 2, LF, UTF-8).
// in_path and out_path must be absolute paths, as the Python tool records them.
std::string sidecar_json(const RetimeResult& result, const std::wstring& in_path, const std::wstring& out_path);

std::string wide_to_utf8(const std::wstring& text);
bool utf8_to_wide(std::string_view text, std::wstring& out);

}  // namespace free_fps
