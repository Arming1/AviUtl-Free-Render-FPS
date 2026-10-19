// Pure re-timer for AviUtl2 .aup2 projects. Port of tools/aup2_fps_upconvert.py (convert()).
// The pass order and the message texts follow the Python tool so that the first refusal is the same.
//
// All rate and frame arithmetic is exact 64-bit rational arithmetic with overflow checks. An
// overflow is reported as a refusal, never as a wrapped value.

#include "retime.h"

#include <windows.h>
#include <bcrypt.h>

#include <algorithm>
#include <cstdio>
#include <map>
#include <numeric>
#include <set>
#include <stdexcept>
#include <utility>

#pragma comment(lib, "bcrypt.lib")

namespace free_fps {
namespace {

using i64 = std::int64_t;
using u32 = std::u32string;

constexpr i64 kI64Max = INT64_MAX;
constexpr i64 kI64Min = INT64_MIN;

// Thrown when 64-bit exact arithmetic would overflow. Converted to a refusal in retime().
struct Overflow {};

// Thrown for every refusal. line is 1-based, 0 when no line applies.
struct Refused {
    std::size_t line;
    std::string reason;
};

[[noreturn]] void refuse(std::size_t line, std::string reason) {
    throw Refused{line, std::move(reason)};
}

// ---------------------------------------------------------------------------
// Checked 64-bit arithmetic and exact rationals
// ---------------------------------------------------------------------------

i64 checked_add(i64 a, i64 b) {
    if ((b > 0 && a > kI64Max - b) || (b < 0 && a < kI64Min - b)) {
        throw Overflow{};
    }
    return a + b;
}

i64 checked_sub(i64 a, i64 b) {
    if ((b < 0 && a > kI64Max + b) || (b > 0 && a < kI64Min + b)) {
        throw Overflow{};
    }
    return a - b;
}

i64 checked_mul(i64 a, i64 b) {
    if (a == 0 || b == 0) {
        return 0;
    }
    if ((a > 0 && b > 0 && a > kI64Max / b) || (a > 0 && b < 0 && b < kI64Min / a) ||
        (a < 0 && b > 0 && a < kI64Min / b) || (a < 0 && b < 0 && a < kI64Max / b)) {
        throw Overflow{};
    }
    return a * b;
}

// Reduced form with den > 0.
Rational make_rational(i64 num, i64 den) {
    if (den == 0 || num == kI64Min || den == kI64Min) {
        throw Overflow{};
    }
    if (den < 0) {
        num = -num;
        den = -den;
    }
    const i64 g = std::gcd(num < 0 ? -num : num, den);
    return Rational{num / g, den / g};
}

Rational rat_mul(const Rational& a, const Rational& b) {
    return make_rational(checked_mul(a.num, b.num), checked_mul(a.den, b.den));
}

Rational rat_div(const Rational& a, const Rational& b) {
    if (b.num == 0) {
        throw Overflow{};  // not reachable: rates are validated as positive
    }
    return make_rational(checked_mul(a.num, b.den), checked_mul(a.den, b.num));
}

Rational rat_sub(const Rational& a, const Rational& b) {
    return make_rational(checked_sub(checked_mul(a.num, b.den), checked_mul(b.num, a.den)),
                         checked_mul(a.den, b.den));
}

bool rat_less(const Rational& a, const Rational& b) {
    return checked_mul(a.num, b.den) < checked_mul(b.num, a.den);
}

Rational rat_abs(Rational a) {
    if (a.num < 0) {
        a.num = -a.num;
    }
    return a;
}

// str(Fraction) in Python: "n" when the denominator is 1, otherwise "n/d".
std::string fraction_str(const Rational& r) {
    if (r.den == 1) {
        return std::to_string(r.num);
    }
    return std::to_string(r.num) + "/" + std::to_string(r.den);
}

// The Python tool's fraction_text(): always "n/d".
std::string fraction_text(const Rational& r) {
    return std::to_string(r.num) + "/" + std::to_string(r.den);
}

i64 floor_div(i64 n, i64 d) {  // d > 0
    i64 q = n / d;
    if (n % d != 0 && n < 0) {
        --q;
    }
    return q;
}

// round_half_up(v * k) for v >= 0 and k > 0, exact: floor(v*k + 1/2) = floor((2n + d) / (2d)).
i64 round_scaled(i64 v, const Rational& k) {
    const Rational p = rat_mul(Rational{v, 1}, k);
    return floor_div(checked_add(checked_mul(2, p.num), p.den), checked_mul(2, p.den));
}

// ---------------------------------------------------------------------------
// Text helpers (ASCII grammar, UTF-8 bytes)
// ---------------------------------------------------------------------------

bool all_digits(std::string_view s) {
    return !s.empty() && std::all_of(s.begin(), s.end(), [](char c) { return c >= '0' && c <= '9'; });
}

// RE_NUMBER: -?\d+(\.\d+)?
bool is_number(std::string_view s) {
    std::size_t i = 0;
    if (i < s.size() && s[i] == '-') {
        ++i;
    }
    const std::size_t int_start = i;
    while (i < s.size() && s[i] >= '0' && s[i] <= '9') {
        ++i;
    }
    if (i == int_start) {
        return false;
    }
    if (i == s.size()) {
        return true;
    }
    if (s[i] != '.') {
        return false;
    }
    ++i;
    const std::size_t frac_start = i;
    while (i < s.size() && s[i] >= '0' && s[i] <= '9') {
        ++i;
    }
    return i > frac_start && i == s.size();
}

// A RE_NUMBER text whose exact value is 0 (0, 0.0, 0.00, -0.0). Checked on the characters: parse_decimal would overflow on a
// fraction of more than 18 digits, which would refuse the whole file instead of the effect.
bool is_zero_number(std::string_view s) {
    return is_number(s) && std::all_of(s.begin(), s.end(), [](char c) { return c == '-' || c == '.' || c == '0'; });
}

// Digits to int64. Refuses when the value does not fit.
i64 parse_digits(std::string_view digits, std::size_t line, const std::string& what) {
    i64 value = 0;
    for (char c : digits) {
        try {
            value = checked_add(checked_mul(value, 10), c - '0');
        } catch (const Overflow&) {
            refuse(line, what + " is too large: '" + std::string(digits) + "'");
        }
    }
    return value;
}

std::vector<std::string> split_on(std::string_view s, char sep) {
    std::vector<std::string> out;
    std::size_t start = 0;
    for (;;) {
        const std::size_t p = s.find(sep, start);
        if (p == std::string_view::npos) {
            out.emplace_back(s.substr(start));
            break;
        }
        out.emplace_back(s.substr(start, p - start));
        start = p + 1;
    }
    return out;
}

std::string replace_all(std::string text, const std::string& from, const std::string& to) {
    std::size_t pos = 0;
    while ((pos = text.find(from, pos)) != std::string::npos) {
        text.replace(pos, from.size(), to);
        pos += to.size();
    }
    return text;
}

// Strict UTF-8 check (RFC 3629): no overlong forms, no surrogates, nothing above U+10FFFF.
bool valid_utf8(std::string_view s) {
    std::size_t i = 0;
    while (i < s.size()) {
        const unsigned char c = static_cast<unsigned char>(s[i]);
        if (c < 0x80) {
            ++i;
            continue;
        }
        std::size_t len = 0;
        if (c >= 0xC2 && c <= 0xDF) {
            len = 2;
        } else if (c >= 0xE0 && c <= 0xEF) {
            len = 3;
        } else if (c >= 0xF0 && c <= 0xF4) {
            len = 4;
        } else {
            return false;
        }
        if (i + len > s.size()) {
            return false;
        }
        for (std::size_t j = 1; j < len; ++j) {
            if ((static_cast<unsigned char>(s[i + j]) & 0xC0) != 0x80) {
                return false;
            }
        }
        const unsigned char c1 = static_cast<unsigned char>(s[i + 1]);
        if ((c == 0xE0 && c1 < 0xA0) || (c == 0xED && c1 > 0x9F) || (c == 0xF0 && c1 < 0x90) ||
            (c == 0xF4 && c1 > 0x8F)) {
            return false;
        }
        i += len;
    }
    return true;
}

// Decodes validated UTF-8 into code points.
u32 decode_utf8(std::string_view s) {
    u32 out;
    std::size_t i = 0;
    while (i < s.size()) {
        const unsigned char c = static_cast<unsigned char>(s[i]);
        std::size_t len = 1;
        char32_t cp = c;
        if (c >= 0xF0) {
            len = 4;
            cp = c & 0x07;
        } else if (c >= 0xE0) {
            len = 3;
            cp = c & 0x0F;
        } else if (c >= 0xC0) {
            len = 2;
            cp = c & 0x1F;
        }
        for (std::size_t j = 1; j < len && i + j < s.size(); ++j) {
            cp = (cp << 6) | (static_cast<unsigned char>(s[i + j]) & 0x3F);
        }
        out.push_back(cp);
        i += len;
    }
    return out;
}

std::string encode_utf8(const u32& s) {
    std::string out;
    for (char32_t cp : s) {
        if (cp < 0x80) {
            out.push_back(static_cast<char>(cp));
        } else if (cp < 0x800) {
            out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        } else if (cp < 0x10000) {
            out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        } else {
            out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        }
    }
    return out;
}

// ---------------------------------------------------------------------------
// Frame-dependent script detection (RE_FRAME_MARKER) with Python's Unicode \w and \s
// ---------------------------------------------------------------------------

// Python \w for str: ASCII letters, digits, '_', and Unicode letters/digits. The Unicode part uses the
// Windows character classes for BMP code points. Supplementary code points are treated as non-word.
// This is an approximation of Unicode categories; see README.md.
bool is_word_cp(char32_t c) {
    if (c < 0x80) {
        return (c >= U'a' && c <= U'z') || (c >= U'A' && c <= U'Z') || (c >= U'0' && c <= U'9') || c == U'_';
    }
    if (c > 0xFFFF) {
        return false;
    }
    const WCHAR wc = static_cast<WCHAR>(c);
    WORD flags = 0;
    if (!GetStringTypeW(CT_CTYPE1, &wc, 1, &flags)) {
        return false;
    }
    return (flags & (C1_ALPHA | C1_DIGIT)) != 0;
}

// Python str.isspace() set, which is what \s matches for str patterns.
bool is_space_cp(char32_t c) {
    return c == 0x20 || (c >= 0x09 && c <= 0x0D) || (c >= 0x1C && c <= 0x1F) || c == 0x85 || c == 0xA0 ||
           c == 0x1680 || (c >= 0x2000 && c <= 0x200A) || c == 0x2028 || c == 0x2029 || c == 0x202F ||
           c == 0x205F || c == 0x3000;
}

bool lit_at(const u32& s, std::size_t i, const char32_t* lit) {
    const std::size_t n = std::char_traits<char32_t>::length(lit);
    return i + n <= s.size() && s.compare(i, n, lit) == 0;
}

// Python \b at position i (between two characters, or at either end).
bool boundary_at(const u32& s, std::size_t i) {
    const bool before = i > 0 && is_word_cp(s[i - 1]);
    const bool after = i < s.size() && is_word_cp(s[i]);
    return before != after;
}

// Matches \s*\( starting at i; end is set to the position after '('.
bool paren_after_spaces(const u32& s, std::size_t i, std::size_t& end) {
    while (i < s.size() && is_space_cp(s[i])) {
        ++i;
    }
    if (i < s.size() && s[i] == U'(') {
        end = i + 1;
        return true;
    }
    return false;
}

// One position of RE_FRAME_MARKER, alternatives tried in the same order as the Python pattern:
//   \bobj\.(?:frame|totalframe|index|rand)\b
//   (?<![\w.])rand\s*\(
//   \bmath\.random\s*\(
//   \bmath\.randomseed\b
bool marker_at(const u32& s, std::size_t i, std::size_t& end) {
    if (boundary_at(s, i) && lit_at(s, i, U"obj.")) {
        static const char32_t* const kWords[] = {U"frame", U"totalframe", U"index", U"rand"};
        for (const char32_t* word : kWords) {
            const std::size_t j = i + 4;
            if (lit_at(s, j, word)) {
                const std::size_t e = j + std::char_traits<char32_t>::length(word);
                if (boundary_at(s, e)) {
                    end = e;
                    return true;
                }
            }
        }
    }
    if (lit_at(s, i, U"rand") && (i == 0 || (!is_word_cp(s[i - 1]) && s[i - 1] != U'.'))) {
        if (paren_after_spaces(s, i + 4, end)) {
            return true;
        }
    }
    if (boundary_at(s, i) && lit_at(s, i, U"math.random") && paren_after_spaces(s, i + 11, end)) {
        return true;
    }
    if (boundary_at(s, i) && lit_at(s, i, U"math.randomseed")) {
        const std::size_t e = i + 15;
        if (boundary_at(s, e)) {
            end = e;
            return true;
        }
    }
    return false;
}

bool find_marker(const u32& s, std::string& match_utf8) {
    for (std::size_t i = 0; i < s.size(); ++i) {
        std::size_t end = 0;
        if (marker_at(s, i, end)) {
            match_utf8 = encode_utf8(s.substr(i, end - i));
            return true;
        }
    }
    return false;
}

// Python unescape_newlines(): literal backslash escapes become newlines.
std::string unescape_newlines(std::string text) {
    text = replace_all(text, "\\r\\n", "\n");
    text = replace_all(text, "\\r", "\n");
    text = replace_all(text, "\\n", "\n");
    return text;
}

// The part of one effect value that is scanned: for テキスト, the text between '<?' and '?>'
// (an unterminated '<?' runs to the end), joined with newlines. Other effects: the whole value.
std::string script_text(const std::optional<std::string>& effect_name, const std::string& key,
                        const std::string& value) {
    static const std::string kText = "テキスト";
    if (effect_name && *effect_name == kText) {
        if (key != kText) {
            return std::string();
        }
        const std::string text = unescape_newlines(value);
        std::string joined;
        std::size_t pos = 0;
        bool first = true;
        while (true) {
            const std::size_t open = text.find("<?", pos);
            if (open == std::string::npos) {
                break;
            }
            const std::size_t start = open + 2;
            const std::size_t close = text.find("?>", start);
            const std::size_t end = close == std::string::npos ? text.size() : close;
            if (!first) {
                joined += "\n";
            }
            joined += text.substr(start, end - start);
            first = false;
            pos = close == std::string::npos ? text.size() : close + 2;
        }
        return joined;
    }
    return value;
}

// ---------------------------------------------------------------------------
// Whitelists (the constants at the top of the Python tool)
// ---------------------------------------------------------------------------

const std::set<std::string> kSceneKnownKeys = {
    "scene", "name", "label",
    "video.width", "video.height", "video.rate", "video.scale",
    "audio.rate", "output.file",
    "cursor.frame", "cursor.layer",
    "preview.frame",
    "display.frame", "display.layer", "display.zoom", "display.order", "display.camera",
    "display.grid.x", "display.grid.y", "display.grid.width", "display.grid.height",
    "display.grid.step", "display.grid.range",
    "display.tempo.bpm", "display.tempo.beat", "display.tempo.start", "display.tempo.offset",
};
const std::set<std::string> kSceneFrameKeys = {"cursor.frame", "preview.frame", "display.frame"};
const std::string kSceneRefuseMarkers[] = {"frame", "mark", "range"};
// イージング（通常）@イージング (full-width parentheses) is script-defined easing: its value depends only on the progress inside
// the object, the parameter and the two end values, so frames are rescaled and values are kept like 直線移動.
// イージング（補間移動）@イージング reads obj.getpoint("time") and is not listed.
const std::string kEasingMode = "イージング（通常）@イージング";
// 直線移動(時間制御) is allowed on any track of any effect: its value depends only on the progress inside the object, so frames are
// rescaled and values are kept like 直線移動 (the time control effect's own 位置 row is handled by plan_time_control).
const std::string kTimeControlMode = "直線移動(時間制御)";
// 補間移動(時間制御) follows the progress inside the object only as well (measured, with or without a curve and with middle
// points), so it is handled like 直線移動(時間制御); precise mode never re-samples it.
const std::string kInterpTimeControlMode = "補間移動(時間制御)";
const std::set<std::string> kTrackModes = {"直線移動", "補間移動", "瞬間移動", "移動無し", "回転", "再生範囲", kEasingMode,
                                           kTimeControlMode, kInterpTimeControlMode};
// Modes that may carry a '|' payload after the parameter (the saved curve or the easing parameter, e.g. "0|1,0,1,0" or
// "0|27"); it is kept as written and not checked. Every other mode refuses it.
const std::set<std::string> kTrackPayloadModes = {"直線移動", kTimeControlMode, kInterpTimeControlMode, kEasingMode};
const std::set<std::string> kTrackCountExempt = {"再生範囲"};
const std::string kCounterEffect = "カウンター";
const std::string kSpeedKey = "速度";
const std::string kColumnKey = "コマ落ち";  // a positive integer; converted only inside a time control effect
const std::string kColumnNoDrop = "1";
// 時間制御(オブジェクト): 位置=a,b,直線移動(時間制御),flag maps the object's range to the controlled layers.
const std::string kTimeControlEffect = "時間制御(オブジェクト)";
const std::string kTimeControlPosition = "位置";
const std::string kTimeControlRefusal = "time control 位置 must be a two-point 直線移動(時間制御) track";
// Frame-counting modes: 反復移動 and ランダム移動 multiply their parameter (a frame count) by k; 移動量指定 divides its second
// value (a step per frame) by k. Written as 'v0,v1,mode,4|parameter' (移動量指定: 'v0,step,mode,4', no '|').
const std::string kFrameCountRepeat = "反復移動";
const std::string kFrameCountRandom = "ランダム移動";
const std::string kFrameCountStep = "移動量指定";
const std::set<std::string> kFrameCountModes = {kFrameCountRepeat, kFrameCountRandom, kFrameCountStep};
const std::string kFrameCountFlag = "4";
constexpr std::size_t kFrameCountMaxPlaces = 6;
const Rational kFrameCountTolerance{1, 2};  // a rewritten value may be off by at most half a frame (355/113 bounds pi above)
// Effects whose look depends on the frame number or on a per-frame random value. Refused in a followed object whose scene
// has k != 1. 手ぶれ and ラスター are not listed: probe 13 (test_kit/13_按帧滤镜, data/probe13_verdict.csv) found that their
// difference between two frame rates follows the time, not the frame number (same moment: 2-4 times the encoding noise;
// same frame number: 100-300 times). 振動 stays listed: it counts its period per frame and is random, so after the period is
// rescaled the same moment still differs (probe 13). ノイズ was not measured and stays refused until a probe lets it through.
// フレア is not listed: with 点滅 = 0 its look does not change over time (probe 13), so kFlareBlinkKey decides it. The 10 3.aup2
// exclusion test (66 -> 60, same AviUtl2 and encoding) did not reduce the significant differences when all 8 フレア objects
// or all 6 手ぶれ effects were removed, so these effects are not the cause of the remaining differences.
const std::set<std::string> kFrameEffectNames = {"振動", "ノイズ"};
// The only フレア 点滅 value that is verified at another frame rate is 0 (a plain number that equals 0). Any other value,
// a track written in 点滅, or a missing 点滅 key is refused in a followed object whose scene has k != 1.
const std::string kFlareEffectName = "フレア";
const std::string kFlareBlinkKey = "点滅";
// The シーン effect plays a scene from its 再生位置 (seconds, or a 再生範囲 trackbar) at 再生速度 and ループ再生. Read by the
// scene alignment (plan_scene_alignment).
const std::string kScenePlayerEffect = "シーン";
const std::string kScenePlayerKey = "シーン";
const std::string kScenePlayPosition = "再生位置";
const std::string kScenePlaySpeed = "再生速度";
const std::string kScenePlayLoop = "ループ再生";
const std::string kPlayRangeMode = "再生範囲";

// ---------------------------------------------------------------------------
// Line structure
// ---------------------------------------------------------------------------

struct Record {
    std::string body;
    std::string eol;  // "\r\n", "\n" or "" (last line without a newline)
};

// Python split_records(): the same split for both CRLF and LF input.
std::vector<Record> split_records(std::string_view text) {
    std::vector<Record> records;
    std::size_t pos = 0;
    while (pos < text.size()) {
        const std::size_t nl = text.find('\n', pos);
        Record rec;
        if (nl == std::string_view::npos) {
            rec.body = std::string(text.substr(pos));
            pos = text.size();
        } else {
            rec.body = std::string(text.substr(pos, nl - pos));
            rec.eol = "\n";
            pos = nl + 1;
        }
        if (!rec.body.empty() && rec.body.back() == '\r') {
            rec.body.pop_back();
            rec.eol = "\r" + rec.eol;
        }
        records.push_back(std::move(rec));
    }
    return records;
}

std::string key_of(const std::string& body) {
    return body.substr(0, body.find('='));
}

std::string value_of(const std::string& body) {
    const std::size_t eq = body.find('=');
    return eq == std::string::npos ? std::string() : body.substr(eq + 1);
}

// RE_SCENE_HDR ^\[scene\.(\d+)\]$, RE_OBJECT_HDR ^\[(\d+)\]$, RE_EFFECT_HDR ^\[(\d+)\.(\d+)\]$
bool scene_header(const std::string& body, std::string& digits) {
    static const std::string kPrefix = "[scene.";
    if (body.size() < kPrefix.size() + 2 || body.compare(0, kPrefix.size(), kPrefix) != 0 || body.back() != ']') {
        return false;
    }
    digits = body.substr(kPrefix.size(), body.size() - kPrefix.size() - 1);
    return all_digits(digits);
}

bool object_header(const std::string& body, std::string& digits) {
    if (body.size() < 3 || body.front() != '[' || body.back() != ']') {
        return false;
    }
    digits = body.substr(1, body.size() - 2);
    return all_digits(digits);
}

bool effect_header(const std::string& body, std::string& first, std::string& second) {
    if (body.size() < 5 || body.front() != '[' || body.back() != ']') {
        return false;
    }
    const std::string inner = body.substr(1, body.size() - 2);
    const std::size_t dot = inner.find('.');
    if (dot == std::string::npos) {
        return false;
    }
    first = inner.substr(0, dot);
    second = inner.substr(dot + 1);
    return all_digits(first) && all_digits(second);
}

// Python is_trackbar(): 'v1,...,vn,mode,param' with at least two numeric values and a non-numeric mode.
bool is_trackbar(const std::string& value) {
    const auto parts = split_on(value, ',');
    if (parts.size() < 4) {
        return false;
    }
    for (std::size_t i = 0; i + 2 < parts.size(); ++i) {
        if (!is_number(parts[i])) {
            return false;
        }
    }
    return !is_number(parts[parts.size() - 2]);
}

// A trackbar value split into its parts. The text after the first '|' (the saved curve or the easing parameter) is the
// payload and may hold commas ("0|1,0,1,0"); is_trackbar() looks at the text before it. param is the flag before the '|'.
struct TrackText {
    std::vector<std::string> values;
    std::string mode;
    std::string param;
    std::optional<std::string> payload;
};

// A value whose text before the first '|' is not a trackbar is judged as a whole (a '|' inside the mode name makes an unknown
// mode that is refused), as it was before payloads were looked at.
bool parse_track_text(const std::string& value, TrackText& out) {
    out.payload.reset();
    const std::size_t bar = value.find('|');
    if (bar != std::string::npos && is_trackbar(value.substr(0, bar))) {
        const auto parts = split_on(value.substr(0, bar), ',');
        out.values.assign(parts.begin(), parts.end() - 2);
        out.mode = parts[parts.size() - 2];
        out.param = parts.back();
        out.payload = value.substr(bar + 1);
        return true;
    }
    if (!is_trackbar(value)) {
        return false;
    }
    const auto parts = split_on(value, ',');
    out.values.assign(parts.begin(), parts.end() - 2);
    out.mode = parts[parts.size() - 2];
    out.param = parts.back();
    return true;
}

// Python parse_frames(): 2 or more non-negative integers.
std::vector<i64> parse_frames(const std::string& value, std::size_t line) {
    const auto items = split_on(value, ',');
    if (items.size() < 2 || !std::all_of(items.begin(), items.end(), all_digits)) {
        refuse(line, "frame= must be 2 or more non-negative integers, got '" + value + "'");
    }
    std::vector<i64> frames;
    for (const auto& item : items) {
        frames.push_back(parse_digits(item, line, "frame= value"));
    }
    return frames;
}

// Python rescale_frames(). Returns the new frame list; shortened is set for a 2-value object that
// became 1 frame.
std::vector<i64> rescale_frames(const std::vector<i64>& frames, const Rational& k, std::size_t line,
                                bool& shortened) {
    shortened = false;
    auto join = [](const std::vector<i64>& v) {
        std::string s;
        for (std::size_t i = 0; i < v.size(); ++i) {
            if (i > 0) {
                s += ",";
            }
            s += std::to_string(v[i]);
        }
        return s;
    };
    if (frames.size() == 2) {
        const i64 start = frames[0];
        const i64 end = frames[1];
        if (end < start) {
            refuse(line, "frame= end " + std::to_string(end) + " is before start " + std::to_string(start));
        }
        const i64 new_start = round_scaled(start, k);
        const i64 new_end = checked_sub(round_scaled(checked_add(end, 1), k), 1);
        if (new_end < new_start) {
            shortened = true;
            return {new_start, new_start};
        }
        return {new_start, new_end};
    }
    for (std::size_t i = 1; i < frames.size(); ++i) {
        if (frames[i] <= frames[i - 1]) {
            refuse(line, "frame= intermediate points are not strictly increasing: " + join(frames));
        }
    }
    std::vector<i64> result;
    result.push_back(round_scaled(frames.front(), k));
    for (std::size_t i = 1; i + 1 < frames.size(); ++i) {
        result.push_back(round_scaled(frames[i], k));
    }
    result.push_back(checked_sub(round_scaled(checked_add(frames.back(), 1), k), 1));
    for (std::size_t i = 1; i < result.size(); ++i) {
        if (result[i] <= result[i - 1]) {
            refuse(line, "frame= points collide or stop increasing after rescaling to the target rate (" +
                             join(frames) + " -> " + join(result) +
                             "); refused rather than merging points");
        }
    }
    return result;
}

struct Prop {
    std::string value;
    std::size_t idx = 0;
};

enum class Kind { kNone, kOther, kProject, kScene, kObject, kEffect };

struct Section {
    Kind kind = Kind::kNone;
    i64 a = 0;
    i64 b = 0;
};

// One [scene.N] section: where its rate lines are, its own rate, and what the re-timer does with it.
struct SceneRate {
    std::size_t rate_idx = 0;
    std::optional<std::size_t> scale_idx;
    i64 rate_value = 0;      // video.rate as written
    i64 scale_value = 1;     // video.scale as written (1 when absent)
    Rational rate;           // rate_value / scale_value in lowest terms
    std::string name;        // name= of the section (the last one), empty when absent
    bool follow = true;      // false: the scene is kept as it is
    Rational k{1, 1};        // target rate / rate for a followed scene, 1 for a kept one
};

struct Mode {
    bool factor = false;
    i64 factor_k = 0;
    Rational target;
    bool precise = false;
    std::size_t precise_extra_decimals = 0;
    SubsceneRatePolicy subscene_rate = SubsceneRatePolicy::Auto;
};

const char* policy_name(SubsceneRatePolicy policy) {
    switch (policy) {
    case SubsceneRatePolicy::Follow:
        return "follow";
    case SubsceneRatePolicy::Keep:
        return "keep";
    case SubsceneRatePolicy::Auto:
        break;
    }
    return "auto";
}

// Python check_layers(): objects are grouped by (scene, layer=). Within a group, sorted by start, a pair that
// touched before must still touch and a pair that did not overlap must not overlap after rescaling.
void check_layers(const std::vector<std::pair<i64, std::size_t>>& frame_lines,
                  const std::map<i64, std::string>& object_layer, const std::map<i64, i64>& object_scene,
                  const std::map<i64, std::vector<i64>>& frame_values,
                  const std::map<i64, std::vector<i64>>& new_frames) {
    using GroupKey = std::pair<i64, std::optional<std::string>>;
    std::vector<std::pair<GroupKey, std::vector<std::pair<i64, std::size_t>>>> groups;
    for (const auto& entry : frame_lines) {
        const i64 obj = entry.first;
        GroupKey key{object_scene.at(obj), std::nullopt};
        const auto layer = object_layer.find(obj);
        if (layer != object_layer.end()) {
            key.second = layer->second;
        }
        auto group = std::find_if(groups.begin(), groups.end(), [&](const auto& g) { return g.first == key; });
        if (group == groups.end()) {
            groups.push_back({key, {}});
            group = groups.end() - 1;
        }
        group->second.push_back(entry);
    }
    for (auto& group : groups) {
        auto& objs = group.second;
        std::stable_sort(objs.begin(), objs.end(), [&](const auto& x, const auto& y) {
            return frame_values.at(x.first).front() < frame_values.at(y.first).front();
        });
        for (std::size_t i = 1; i < objs.size(); ++i) {
            const auto& earlier = objs[i - 1];
            const auto& later = objs[i];
            const i64 old_end = frame_values.at(earlier.first).back();
            const i64 old_start = frame_values.at(later.first).front();
            if (old_end >= old_start) {
                continue;
            }
            const i64 new_end = new_frames.at(earlier.first).back();
            const i64 new_start = new_frames.at(later.first).front();
            const std::size_t line = later.second + 1;
            const std::size_t other = earlier.second + 1;
            if (old_end + 1 == old_start) {
                if (new_end + 1 != new_start) {
                    refuse(line, "object touches the object at line " + std::to_string(other) +
                                     " on the same scene and layer, but after rescaling they no longer touch "
                                     "(one of them became 1 frame); refused");
                }
            } else if (new_end >= new_start) {
                refuse(line, "object overlaps the object at line " + std::to_string(other) +
                                 " on the same scene and layer after rescaling; refused");
            }
        }
    }
}

// ---------------------------------------------------------------------------
// Precise mode: exact re-sampling of trackbars (exact rationals, no floating point)
// Port of precise_resample() in tools/aup2_fps_upconvert.py. One deliberate difference: a 2-point object with
// frame= start == end is skipped (the Python tool divides by zero there).
// ---------------------------------------------------------------------------

Rational rat_add(const Rational& a, const Rational& b) {
    return make_rational(checked_add(checked_mul(a.num, b.den), checked_mul(b.num, a.den)),
                         checked_mul(a.den, b.den));
}

bool rat_equal(const Rational& a, const Rational& b) {
    return a.num == b.num && a.den == b.den;  // both reduced
}

i64 rat_floor(const Rational& r) {
    return floor_div(r.num, r.den);
}

i64 rat_ceil(const Rational& r) {
    return -floor_div(-r.num, r.den);  // num is never INT64_MIN (make_rational)
}

Rational rat_clamp(const Rational& x, const Rational& low, const Rational& high) {
    if (rat_less(x, low)) {
        return low;
    }
    if (rat_less(high, x)) {
        return high;
    }
    return x;
}

i64 pow10_checked(std::size_t d) {
    i64 p = 1;
    for (std::size_t i = 0; i < d; ++i) {
        p = checked_mul(p, 10);
    }
    return p;
}

// RE_NUMBER text (validated by is_number) to an exact rational. fraction_digits gets the digits after the point.
Rational parse_decimal(const std::string& text, std::size_t& fraction_digits) {
    const bool negative = !text.empty() && text.front() == '-';
    const std::string body = negative ? text.substr(1) : text;
    const std::size_t dot = body.find('.');
    std::string digits = body;
    fraction_digits = 0;
    if (dot != std::string::npos) {
        digits = body.substr(0, dot) + body.substr(dot + 1);
        fraction_digits = body.size() - dot - 1;
    }
    i64 value = 0;
    for (char c : digits) {
        value = checked_add(checked_mul(value, 10), c - '0');
    }
    return make_rational(negative ? -value : value, pow10_checked(fraction_digits));
}

// x * 10^d rounded half away from zero, as an integer. Throws Overflow when it does not fit.
i64 quantize_scaled(const Rational& x, std::size_t d) {
    const i64 scale = pow10_checked(d);
    const i64 magnitude = checked_mul(x.num < 0 ? -x.num : x.num, scale);
    i64 q = magnitude / x.den;
    const i64 rest = magnitude % x.den;
    if (rest >= x.den - rest) {  // rest * 2 >= den, without overflow
        q = checked_add(q, 1);
    }
    return x.num < 0 ? -q : q;
}

// A scaled integer (value * 10^d) as text with exactly d fraction digits: -20000 with d = 2 gives "-200.00".
// Zero is never negative.
std::string format_fixed(i64 scaled, std::size_t d) {
    const bool negative = scaled < 0;
    const i64 magnitude = negative ? -scaled : scaled;
    const i64 scale = pow10_checked(d);
    std::string text = (negative ? "-" : "") + std::to_string(magnitude / scale);
    if (d > 0) {
        const std::string fraction = std::to_string(magnitude % scale);
        text += "." + std::string(d - fraction.size(), '0') + fraction;
    }
    return text;
}

// Value of the piecewise-linear curve through (pts[i], vals[i]) at t, exact. t <= pts.front() uses the first
// segment and t >= pts.back() the last one (straight-line extrapolation). pts is strictly increasing, size >= 2.
Rational track_value(const std::vector<i64>& pts, const std::vector<Rational>& vals, const Rational& t) {
    const std::size_t last = pts.size() - 2;  // index of the last segment
    std::size_t seg = last;
    if (!rat_less(Rational{pts.front(), 1}, t)) {
        seg = 0;
    } else if (rat_less(t, Rational{pts.back(), 1})) {
        for (std::size_t i = 0; i <= last; ++i) {
            if (rat_less(t, Rational{pts[i + 1], 1})) {
                seg = i;
                break;
            }
        }
    }
    const Rational span{checked_sub(pts[seg + 1], pts[seg]), 1};
    const Rational ratio = rat_div(rat_sub(t, Rational{pts[seg], 1}), span);
    return rat_add(vals[seg], rat_mul(rat_sub(vals[seg + 1], vals[seg]), ratio));
}

// The same curve held at both ends: before the first keyframe the first value, after the last keyframe the last value
// (an object keeps its last value after its last frame, the track does not continue its line).
Rational track_value_held(const std::vector<i64>& pts, const std::vector<Rational>& vals, const Rational& t) {
    return track_value(pts, vals, rat_clamp(t, Rational{pts.front(), 1}, Rational{pts.back(), 1}));
}

// The keyframes written for a held object: the rescaled keyframes (nf) plus the points where the target
// v(clamp(n / k, s, e)) bends because of the clamp, so that the value stops at the original end value (and starts at the
// original start value) instead of continuing the line. n / k is the original frame of the new frame n; s and e are the
// first and last original keyframe, s' and e' the first and last new one.
//   End, when e' / k > e: m = floor(e k) (at least s'). m gets v(m / k) and, when e k is not an integer, m + 1 gets v(e);
//   every later frame up to e' is v(e) as well.
//   Start, when s' / k < s: s' is v(s); with n0 = ceil(s k) <= e', n0 gets v(n0 / k) and n0 - 1 (when it is after s')
//   gets v(s).
// An object that was shortened to one frame (equal frames) has no segment and is returned as it is.
std::vector<i64> hold_keyframes(const std::vector<i64>& pts, const std::vector<i64>& nf, const Rational& k) {
    const i64 s2 = nf.front();
    const i64 e2 = nf.back();
    if (e2 <= s2) {
        return nf;
    }
    std::set<i64> keys(nf.begin(), nf.end());
    const Rational start_scaled = rat_mul(Rational{pts.front(), 1}, k);  // s k
    const Rational end_scaled = rat_mul(Rational{pts.back(), 1}, k);     // e k
    if (rat_less(end_scaled, Rational{e2, 1})) {
        const i64 m = (std::max)(rat_floor(end_scaled), s2);
        keys.insert(m);
        if (end_scaled.den != 1 && m + 1 <= e2) {
            keys.insert(m + 1);
        }
    }
    if (rat_less(Rational{s2, 1}, start_scaled)) {
        const i64 n0 = rat_ceil(start_scaled);
        if (n0 <= e2) {
            keys.insert(n0);
            if (n0 - 1 > s2) {
                keys.insert(n0 - 1);
            }
        }
    }
    return std::vector<i64>(keys.begin(), keys.end());
}

// One trackbar row of an object: 'name=v0,...,vn,mode,param'.
struct TrackLine {
    std::size_t idx = 0;
    std::string name;
    std::string mode;
    std::string param;
    std::optional<std::string> payload;  // the text after '|', kept as written
    std::vector<std::string> values;
};

// Python precise_track_problem(), in the same order, plus the zero-length rule. Empty when the row can be
// resampled. The reason names the row's own line, as the Python tool does. A payload is kept when the row is resampled
// (直線移動 only). 直線移動(時間制御) is resampled like 直線移動 when it has two points and no payload (the object-level check
// for the end hold, which that mode cannot take, is in apply_precise).
std::string precise_problem(const TrackLine& track, const std::vector<Rational>& vals, const std::vector<i64>& pts) {
    const std::string line = std::to_string(track.idx + 1);
    if (kFrameCountModes.count(track.mode) != 0) {
        return "trackbar at line " + line + " uses mode " + track.mode + "; frame-counting modes are not re-sampled";
    }
    if (track.mode == kTimeControlMode) {
        if (track.payload || vals.size() != 2) {
            return "trackbar at line " + line + " uses mode " + track.mode +
                   " with a curve or keyframes; it is not re-sampled";
        }
    } else if (track.mode == kInterpTimeControlMode) {
        return "trackbar at line " + line + " uses mode " + track.mode + "; it is not re-sampled";
    } else if (track.payload && track.mode != "直線移動") {
        return "trackbar at line " + line + " has a '|' payload";
    }
    bool constant = true;
    for (const Rational& v : vals) {
        constant = constant && rat_equal(v, vals.front());
    }
    const bool linear = (track.mode == "直線移動" || track.mode == kTimeControlMode) && track.param == "0";
    if (!linear && !constant) {
        return "trackbar at line " + line + " uses mode " + track.mode + " with parameter " + track.param +
               " and changing values; only 直線移動,0 or constant values can be resampled exactly";
    }
    if (pts.size() == 2 && pts[0] == pts[1]) {
        return "frame= start equals end; there is no segment to resample";
    }
    return std::string();
}

// Precise mode. Objects are visited in frame= order and tracks in file order. An object with no trackbar rows
// (other than '再生範囲') is neither applied nor skipped. An object is skipped when one of its rows has a problem
// (its frame= line is recorded); otherwise each row is re-sampled at the new keyframes: t = f'/k, value from the
// exact curve through the old keyframes (k is the object's own scene factor). Values are rounded to the largest decimal count of the row plus
// extra_decimals. The report figures are 6-decimal strings, the range is max - min of the old values.
// The value shown is the original one at the same moment: the original curve held at its end values (an object keeps
// its last value after its last frame). The keyframes are the rescaled ones plus the points where the clamp bends the
// target (hold_keyframes); an applied object's frame= list is then the longer list (written_frames, by object). An
// object that has a 時間制御(オブジェクト) effect is left alone (neither applied nor skipped): plan_time_control owns its
// frame= list.
// Returns the rewritten rows by line index; the caller writes them (res.changes) after the frame= rows.
std::map<std::size_t, std::string> apply_precise(
    const std::vector<std::string>& lines, const std::vector<std::size_t>& effect_lines,
    const std::vector<Section>& section_of, const std::vector<std::pair<i64, std::size_t>>& frame_lines,
    const std::map<i64, std::vector<i64>>& frame_values, const std::map<i64, std::vector<i64>>& new_frames,
    const std::map<i64, Rational>& k_of, const std::set<std::pair<i64, i64>>& time_control_sections,
    std::size_t extra_decimals, std::map<i64, std::vector<i64>>& written_frames, RetimeResult& res) {
    res.precise.emplace();
    PreciseReport& report = *res.precise;
    report.extra_decimals = extra_decimals;

    std::map<i64, std::vector<TrackLine>> tracks_of;  // object -> its trackbar rows, file order
    for (std::size_t idx : effect_lines) {
        // The 位置 row of a time control effect is already exact (plan_time_control); it is neither re-sampled nor
        // reported as skipped.
        if (time_control_sections.count(std::make_pair(section_of[idx].a, section_of[idx].b)) != 0) {
            continue;
        }
        TrackText text;
        if (!parse_track_text(value_of(lines[idx]), text) || kTrackCountExempt.count(text.mode) != 0) {
            continue;
        }
        TrackLine track;
        track.idx = idx;
        track.name = key_of(lines[idx]);
        track.mode = text.mode;
        track.param = text.param;
        track.payload = text.payload;
        track.values = text.values;
        tracks_of[section_of[idx].a].push_back(std::move(track));
    }

    std::set<i64> time_control_objects;
    for (const auto& section : time_control_sections) {
        time_control_objects.insert(section.first);
    }

    std::map<std::size_t, std::string> bodies;  // rewritten rows by line index
    for (const auto& [obj, frame_idx] : frame_lines) {
        const auto found = tracks_of.find(obj);
        if (found == tracks_of.end()) {
            continue;
        }
        if (time_control_objects.count(obj) != 0) {
            continue;
        }
        const std::vector<i64>& pts = frame_values.at(obj);
        const std::vector<i64>& nf = new_frames.at(obj);
        const Rational& k = k_of.at(obj);

        std::vector<std::vector<Rational>> parsed;
        std::vector<std::size_t> decimals;
        std::string problem;
        for (const TrackLine& track : found->second) {
            std::vector<Rational> vals;
            std::size_t most = 0;
            for (const std::string& text : track.values) {
                std::size_t fraction_digits = 0;
                vals.push_back(parse_decimal(text, fraction_digits));
                most = (std::max)(most, fraction_digits);
            }
            problem = precise_problem(track, vals, pts);
            if (!problem.empty()) {
                break;
            }
            parsed.push_back(std::move(vals));
            decimals.push_back(most);
        }
        const std::vector<i64> keys = hold_keyframes(pts, nf, k);
        if (problem.empty() && keys != nf) {
            // 直線移動(時間制御) ignores intermediate points in time, so keyframes added for the end hold would change it.
            for (const TrackLine& track : found->second) {
                if (track.mode == kTimeControlMode) {
                    problem = "trackbar at line " + std::to_string(track.idx + 1) + " uses mode " + track.mode +
                              "; end hold needs keyframes this mode ignores";
                    break;
                }
            }
        }
        if (!problem.empty()) {
            report.skipped.push_back(PreciseSkip{frame_idx + 1, problem});
            continue;
        }
        report.applied += 1;
        if (keys != nf) {
            written_frames[obj] = keys;
        }

        for (std::size_t t = 0; t < found->second.size(); ++t) {
            const TrackLine& track = found->second[t];
            const std::vector<Rational>& vals = parsed[t];
            const std::size_t places = decimals[t] + extra_decimals;
            bool constant = true;
            for (const Rational& v : vals) {
                constant = constant && rat_equal(v, vals.front());
            }

            Rational max_error{0, 1};
            std::vector<std::string> texts;
            i64 last_units = 0;
            for (i64 f : keys) {
                const Rational x =
                    constant ? vals.front() : track_value_held(pts, vals, rat_div(Rational{f, 1}, k));
                last_units = quantize_scaled(x, places);
                const Rational error = rat_abs(rat_sub(make_rational(last_units, pow10_checked(places)), x));
                if (rat_less(max_error, error)) {
                    max_error = error;
                }
                texts.push_back(format_fixed(last_units, places));
            }

            Rational low = vals.front();
            Rational high = vals.front();
            for (const Rational& v : vals) {
                if (rat_less(v, low)) {
                    low = v;
                }
                if (rat_less(high, v)) {
                    high = v;
                }
            }
            const Rational range = rat_sub(high, low);
            Rational ratio{0, 1};
            if (range.num != 0) {
                ratio = rat_div(max_error, range);
            }
            const Rational percent = rat_mul(ratio, Rational{100, 1});
            const std::string pct_text = format_fixed(quantize_scaled(percent, 6), 6);
            const Rational end_shift = rat_sub(make_rational(last_units, pow10_checked(places)), vals.back());

            std::string joined;
            for (std::size_t i = 0; i < texts.size(); ++i) {
                joined += (i > 0 ? "," : "") + texts[i];
            }
            bodies[track.idx] = track.name + "=" + joined + "," + track.mode + "," + track.param +
                                (track.payload ? "|" + *track.payload : std::string());

            const std::string error_text = format_fixed(quantize_scaled(max_error, 6), 6);
            const std::string end_text = format_fixed(quantize_scaled(end_shift, 6), 6);
            report.tracks.push_back(PreciseTrack{frame_idx + 1, track.idx + 1, error_text, pct_text, end_text});
            if (rat_less(Rational{1, 10000}, ratio)) {  // above 0.01 % of the range
                res.notes.push_back("precise: trackbar at line " + std::to_string(track.idx + 1) +
                                    " (object at line " + std::to_string(frame_idx + 1) +
                                    ") quantization error is " + pct_text +
                                    "% of its range, above 0.01%");
            }
        }
    }

    return bodies;
}

// ---------------------------------------------------------------------------
// Time control (時間制御(オブジェクト)) and コマ落ち, exact rationals
// The effect sits on a lower layer than the layers it controls. With the object range s..e (L = e - s + 1) and
// 位置=a,b, the host clamps a and b to 0..100 (the end values only, not each frame), maps a timeline frame f of the
// range to p(f) = a + (b - a) (f - s) / (L - 1) percent of the object's own range and shows the controlled layers at
// tau(f) = s + p(f) / 100 (L - 1). コマ落ち=K shows tau of the first frame of each K-frame segment (g(f) = s +
// floor((f - s) / K) K). The re-timer keeps the controlled time, in seconds, equal to the original at every output
// frame n: the target is T(n) = tau(g(clamp(n / k, s, e))) and the new 位置 must give s' + p'(n) / 100 (L' - 1) = k T(n).
//   Route A (a straight 直線移動(時間制御) track): when K is 1, or K x k and s x k are integers, and the plain line
//   p'(n) = 100 (k tau(n / k) - s') / (L' - 1) stays in 0..100 at s' and e', only 位置 (and K x k) is rewritten.
//   Route B (steps): otherwise 位置 becomes a 瞬間移動 track with one point where the clamped, rounded p'(n) changes;
//   the object's frame= list holds those frames and コマ落ち becomes 1. The host never shows the last point, so the
//   last frame shows the value of the point before it.
// ---------------------------------------------------------------------------

constexpr std::size_t kMaxTimeControlSteps = 4096;
const std::string kStepMode = "瞬間移動";

struct TimeControl {
    i64 obj = 0;
    i64 effect = 0;
    std::size_t pos_idx = 0;  // the 位置 line
    std::string a_text;
    std::string b_text;
    std::string flag;
    bool curve = false;  // the 位置 row carries a '|' payload (a curve edited in the time control dialog)
};

// One time control object: the original range s..e, the new range s'..e', the end values of 位置 clamped to 0..100,
// the factor k of its scene and the number of frames per コマ落ち segment (1: none).
struct TcGeometry {
    i64 s = 0;
    i64 e = 0;
    i64 s2 = 0;
    i64 e2 = 0;
    Rational a;
    Rational b;
    Rational k;
    i64 drop = 1;
};

// 位置 of the original line at the (real) timeline frame f.
Rational tc_line_percent(const TcGeometry& g, const Rational& f) {
    if (g.e == g.s) {
        return g.a;
    }
    const i64 span = checked_sub(g.e, g.s);
    return rat_add(g.a, rat_mul(rat_sub(g.b, g.a), rat_div(rat_sub(f, Rational{g.s, 1}), Rational{span, 1})));
}

// tau: the controlled timeline frame for a 位置 value p (L = 1: always s).
Rational tc_controlled_frame(const TcGeometry& g, const Rational& p) {
    const i64 span = checked_sub(g.e, g.s);
    return rat_add(Rational{g.s, 1}, rat_mul(rat_div(p, Rational{100, 1}), Rational{span, 1}));
}

// The new 位置 that shows the controlled frame k tau on the new range (L' >= 2).
Rational tc_percent_for(const TcGeometry& g, const Rational& tau) {
    const Rational shift = rat_sub(rat_mul(g.k, tau), Rational{g.s2, 1});
    return rat_div(rat_mul(shift, Rational{100, 1}), Rational{checked_sub(g.e2, g.s2), 1});
}

// Route A: 位置 (unclamped) at the new frame n from the plain line, s' + p'(n) / 100 (L' - 1) = k tau(n / k). A 1-frame
// new range ignores p' in the host (the controlled time is s'); p of the original line at the same moment (the line
// continued past s..e) is written then.
Rational tc_linear_percent(const TcGeometry& g, i64 n) {
    const Rational f = rat_div(Rational{n, 1}, g.k);  // the original frame at the same moment (not an integer)
    if (g.e2 == g.s2) {
        return tc_line_percent(g, f);
    }
    return tc_percent_for(g, tc_controlled_frame(g, tc_line_percent(g, f)));
}

// Route B: 位置 the host needs at the new frame n (L' >= 2), clamped to 0..100.
Rational tc_step_percent(const TcGeometry& g, i64 n) {
    Rational f = rat_clamp(rat_div(Rational{n, 1}, g.k), Rational{g.s, 1}, Rational{g.e, 1});
    if (g.drop > 1) {
        const i64 segment = rat_floor(rat_div(rat_sub(f, Rational{g.s, 1}), Rational{g.drop, 1}));
        f = rat_add(Rational{g.s, 1}, Rational{checked_mul(segment, g.drop), 1});
    }
    const Rational p = tc_percent_for(g, tc_controlled_frame(g, tc_line_percent(g, f)));
    return rat_clamp(p, Rational{0, 1}, Rational{100, 1});
}

bool percent_in_range(const Rational& p) {
    return !rat_less(p, Rational{0, 1}) && !rat_less(Rational{100, 1}, p);
}

// A trackbar row next to the 位置 row of an object that gets steps (its frame= list gets more points).
struct RepeatedTrack {
    std::size_t idx = 0;
    std::string first_value;  // the first value as written; every value of the row is equal to it
    std::string tail;         // ",mode,param"
};

// The other trackbar rows of the object (the 位置 row at pos_idx and 再生範囲 rows excepted; a 再生範囲 row has no value per
// frame point and stays): a row with changing values cannot follow the new frame= list and refuses; a row whose values
// are all equal is repeated at every point (see repeated_row).
std::vector<RepeatedTrack> other_trackbars(const std::vector<std::string>& lines,
                                           const std::vector<std::size_t>& object_effect_lines, std::size_t pos_idx) {
    std::vector<RepeatedTrack> rows;
    for (std::size_t idx : object_effect_lines) {
        if (idx == pos_idx) {
            continue;
        }
        TrackText text;
        if (!parse_track_text(value_of(lines[idx]), text) || kTrackCountExempt.count(text.mode) != 0) {
            continue;
        }
        std::size_t digits = 0;
        const Rational first = parse_decimal(text.values[0], digits);
        for (std::size_t i = 1; i < text.values.size(); ++i) {
            if (!rat_equal(parse_decimal(text.values[i], digits), first)) {
                refuse(0, "time control at line " + std::to_string(pos_idx + 1) +
                              ": the object has other animated trackbars");
            }
        }
        rows.push_back(RepeatedTrack{idx, text.values[0],
                                     "," + text.mode + "," + text.param +
                                         (text.payload ? "|" + *text.payload : std::string())});
    }
    return rows;
}

// The new text of each 位置 row (every time control effect of a followed scene, in file order), the new コマ落ち rows
// and one note per effect that changes. A route B effect also gets the longer frame= list of its object
// (written_frames). Returns the rewritten rows by line index; the caller writes them (res.changes) after precise mode.
std::map<std::size_t, std::string> plan_time_control(
    const std::vector<std::string>& lines, std::vector<TimeControl> controls,
    const std::vector<std::size_t>& effect_lines, const std::vector<Section>& section_of,
    const std::map<std::pair<i64, i64>, std::map<std::string, Prop>>& effect_props,
    const std::map<i64, std::size_t>& frame_line_of, const std::map<i64, std::vector<i64>>& frame_values,
    const std::map<i64, std::vector<i64>>& new_frames, const std::map<i64, Rational>& k_of,
    std::map<i64, std::vector<i64>>& written_frames, RetimeResult& res) {
    std::map<std::size_t, std::string> bodies;
    std::map<i64, std::vector<std::size_t>> effect_lines_of;  // object -> its effect lines, file order
    for (std::size_t idx : effect_lines) {
        effect_lines_of[section_of[idx].a].push_back(idx);
    }
    std::sort(controls.begin(), controls.end(),
              [](const TimeControl& x, const TimeControl& y) { return x.pos_idx < y.pos_idx; });
    for (const TimeControl& tc : controls) {
        const std::vector<i64>& old = frame_values.at(tc.obj);
        const std::vector<i64>& nw = new_frames.at(tc.obj);
        std::size_t a_digits = 0;
        std::size_t b_digits = 0;
        const Rational a = parse_decimal(tc.a_text, a_digits);
        const Rational b = parse_decimal(tc.b_text, b_digits);
        const std::size_t places = (std::max)(a_digits, b_digits);

        TcGeometry g;
        g.s = old.front();
        g.e = old.back();
        g.s2 = nw.front();
        g.e2 = nw.back();
        g.a = rat_clamp(a, Rational{0, 1}, Rational{100, 1});
        g.b = rat_clamp(b, Rational{0, 1}, Rational{100, 1});
        g.k = k_of.at(tc.obj);
        if (g.k.num == 1 && g.k.den == 1) {
            continue;  // a scene that keeps its rate is not touched
        }

        const auto props = effect_props.find(std::make_pair(tc.obj, tc.effect));
        const auto drop_prop = props->second.find(kColumnKey);
        if (drop_prop != props->second.end()) {
            g.drop = parse_digits(drop_prop->second.value, drop_prop->second.idx + 1, kColumnKey + "= value");
        }
        const Rational scaled_drop = rat_mul(Rational{g.drop, 1}, g.k);  // K x k
        const Rational scaled_start = rat_mul(Rational{g.s, 1}, g.k);    // s x k
        const bool one_frame = g.s2 == g.e2;
        if (tc.curve) {
            // A curve is a function of the progress inside the object, so the row stays as it is; only the frame= list
            // and a コマ落ち K x k change. K x k and s x k must be exact (a staircase cannot follow a curve).
            const std::string position_line = std::to_string(tc.pos_idx + 1);
            std::string note = "time control at line " + std::to_string(frame_line_of.at(tc.obj) + 1) + ": " +
                               kTimeControlPosition + " has a curve; kept (object-relative, within one frame)";
            if (g.drop > 1) {
                if (scaled_drop.den != 1 || scaled_start.den != 1) {
                    refuse(0, "time control at line " + position_line + " has a curve and " + kColumnKey + "=" +
                                  std::to_string(g.drop) + " cannot be kept exact");
                }
                if (scaled_drop.num != g.drop) {
                    bodies[drop_prop->second.idx] = kColumnKey + "=" + std::to_string(scaled_drop.num);
                    note += "; " + kColumnKey + " " + std::to_string(g.drop) + " -> " + std::to_string(scaled_drop.num);
                }
            }
            res.notes.push_back(note);
            continue;
        }
        const bool drop_exact = g.drop == 1 || (scaled_drop.den == 1 && scaled_start.den == 1);

        const Rational first = tc_linear_percent(g, g.s2);
        const Rational last = tc_linear_percent(g, g.e2);
        const bool route_a = one_frame || (drop_exact && percent_in_range(first) && percent_in_range(last));
        const std::string key = key_of(lines[tc.pos_idx]);
        const std::string position_line = std::to_string(tc.pos_idx + 1);

        std::string note = "time control at line " + std::to_string(frame_line_of.at(tc.obj) + 1) + ": " +
                           kTimeControlPosition + " " + tc.a_text + "," + tc.b_text + " -> ";
        std::string last_frame_hint;
        std::optional<i64> new_drop;  // the new コマ落ち value, when the row changes
        bool changed = false;
        if (route_a) {
            const std::string first_text = format_fixed(quantize_scaled(first, places), places);
            const std::string last_text = format_fixed(quantize_scaled(last, places), places);
            const std::string new_body =
                key + "=" + first_text + "," + last_text + "," + kTimeControlMode + "," + tc.flag;
            changed = new_body != lines[tc.pos_idx];
            if (changed) {
                bodies[tc.pos_idx] = new_body;
            }
            note += first_text + "," + last_text;
            if (g.drop > 1) {
                // A single new frame shows its first frame whatever the segment length is; any other range needs K x k.
                const i64 wanted = scaled_drop.den == 1 ? scaled_drop.num : 1;
                if (wanted != g.drop) {
                    new_drop = wanted;
                }
            }
        } else {
            const std::string too_many = "time control at line " + position_line + " needs more than " +
                                         std::to_string(kMaxTimeControlSteps) + " steps";
            const std::vector<RepeatedTrack> others = other_trackbars(lines, effect_lines_of[tc.obj], tc.pos_idx);
            std::vector<i64> points;
            std::vector<std::string> texts;
            std::string previous;
            std::string text;
            for (i64 n = g.s2; n <= g.e2; ++n) {
                text = format_fixed(quantize_scaled(tc_step_percent(g, n), places), places);
                if (n == g.s2 || text != previous) {
                    points.push_back(n);
                    texts.push_back(text);
                    if (points.size() > kMaxTimeControlSteps) {
                        refuse(0, too_many);
                    }
                }
                previous = text;
            }
            const bool last_is_step = points.back() == g.e2;
            if (!last_is_step) {
                points.push_back(g.e2);
                texts.push_back(text);
            }
            if (points.size() > kMaxTimeControlSteps) {
                refuse(0, too_many);
            }
            for (const RepeatedTrack& other : others) {
                std::string row = key_of(lines[other.idx]) + "=";
                for (std::size_t i = 0; i < points.size(); ++i) {
                    row += other.first_value + (i + 1 < points.size() ? "," : "");
                }
                bodies[other.idx] = row + other.tail;
            }
            std::string joined;
            for (std::size_t i = 0; i < texts.size(); ++i) {
                joined += (i > 0 ? "," : "") + texts[i];
            }
            bodies[tc.pos_idx] = key + "=" + joined + "," + kStepMode + ",0";
            written_frames[tc.obj] = points;
            changed = true;
            note += std::to_string(points.size()) + " steps (" + kStepMode + ")";
            if (g.drop > 1) {
                new_drop = 1;
            }
            if (last_is_step) {
                last_frame_hint = "; last frame shows the previous step";
            }
        }
        if (new_drop) {
            bodies[drop_prop->second.idx] = kColumnKey + "=" + std::to_string(*new_drop);
            note += "; " + kColumnKey + " " + std::to_string(g.drop) + " -> " + std::to_string(*new_drop);
            changed = true;
        }
        if (changed) {
            res.notes.push_back(note + last_frame_hint);
        }
    }
    return bodies;
}

// ---------------------------------------------------------------------------
// Frame-counting trackbar modes (反復移動, ランダム移動, 移動量指定), exact. The checks of phase 1 run in the trackbar loop
// of convert() (file order, followed scenes only); the rewrite (phase 2) runs after check_layers and before the time control.
// A row of a scene with k = 1 is checked in phase 1 only.
// ---------------------------------------------------------------------------

// A frame-counting row of a followed object, as checked in phase 1.
struct FrameModeRow {
    std::size_t idx = 0;
    i64 obj = 0;
    std::string mode;
    std::vector<std::string> values;  // the values as written
    std::string payload;              // 反復移動 and ランダム移動: the parameter after '|' as written (empty for 移動量指定)
};

// Phase 1 for one row. frames is the frame= list of its object (null when it has none). Refuses at the first rule that
// fails. Returns the parameter after '|' (empty for 移動量指定).
std::string check_frame_mode(std::size_t line, const TrackText& text, const std::vector<i64>* frames) {
    const std::string& mode = text.mode;
    // The Python tool's param: the text after the last comma of the value, '|' payload included.
    const std::string param = text.param + (text.payload ? "|" + *text.payload : std::string());
    const std::size_t bar = param.find('|');
    const bool has_bar = bar != std::string::npos;
    const std::string base = has_bar ? param.substr(0, bar) : param;
    const std::string payload = has_bar ? param.substr(bar + 1) : std::string();
    if (mode == kFrameCountStep && has_bar) {
        refuse(line, mode + " takes no '|' payload, got " + "'" + param + "'");
    }
    if (mode != kFrameCountStep && !has_bar) {
        refuse(line, mode + " needs a '|' parameter, got " + "'" + param + "'");
    }
    if (base != kFrameCountFlag) {
        refuse(line, mode + " flag must be " + kFrameCountFlag + ", got " + "'" + base + "'");
    }
    if (text.values.size() != 2 || frames == nullptr || frames->size() != 2) {
        refuse(line, mode + " is accepted only with two frame points and two values (middle points are not verified)");
    }
    if (mode == kFrameCountStep) {
        return std::string();
    }
    std::size_t digits = 0;
    if (!is_number(payload) || rat_less(parse_decimal(payload, digits), Rational{1, 1})) {
        refuse(line, mode + " parameter must be a number of at least 1, got " + "'" + payload + "'");
    }
    if (mode == kFrameCountRandom && parse_decimal(payload, digits).den != 1) {
        refuse(line, mode + " parameter must be an integer, got " + "'" + payload + "'");
    }
    return payload;
}

// Unsigned big integer (32-bit limbs, least significant first, no leading zero limbs). Used only by error_exceeds_half(),
// where a product of several 64-bit factors can exceed 64 bits; it cannot overflow.
using BigUint = std::vector<std::uint32_t>;

BigUint big_from(std::uint64_t v) {
    BigUint out;
    while (v != 0) {
        out.push_back(static_cast<std::uint32_t>(v));
        v >>= 32;
    }
    return out;
}

void big_trim(BigUint& a) {
    while (!a.empty() && a.back() == 0) {
        a.pop_back();
    }
}

BigUint big_mul(const BigUint& a, const BigUint& b) {
    if (a.empty() || b.empty()) {
        return BigUint();
    }
    BigUint out(a.size() + b.size(), 0);
    for (std::size_t i = 0; i < a.size(); ++i) {
        std::uint64_t carry = 0;
        for (std::size_t j = 0; j < b.size(); ++j) {
            // a[i] * b[j] + out + carry is at most 2^64 - 1, so it fits.
            const std::uint64_t t = static_cast<std::uint64_t>(a[i]) * b[j] + out[i + j] + carry;
            out[i + j] = static_cast<std::uint32_t>(t);
            carry = t >> 32;
        }
        out[i + b.size()] = static_cast<std::uint32_t>(carry);
    }
    big_trim(out);
    return out;
}

// Trimmed values: -1, 0 or 1 as a < b, a == b, a > b.
int big_cmp(const BigUint& a, const BigUint& b) {
    if (a.size() != b.size()) {
        return a.size() < b.size() ? -1 : 1;
    }
    for (std::size_t i = a.size(); i-- > 0;) {
        if (a[i] != b[i]) {
            return a[i] < b[i] ? -1 : 1;
        }
    }
    return 0;
}

// Whether E > 1/2 for E = |x| x 355/113 x frames x |y| / (2 x m^2), the error bound of a 反復移動 / ランダム移動 row (SPEC:
// |e - s| x PI_UPPER x F x |Q - P2| / (2 x min(Q, P2)^2), PI_UPPER = 355/113). Exact: with x = xn/xd, y = yn/yd and m = mn/md
// (lowest terms, positive m), E > 1/2 is 355 x F x xn x yn x md^2 > 113 x mn^2 x xd x yd, compared as big integers.
bool error_exceeds_half(const Rational& x, i64 frames, const Rational& y, const Rational& m) {
    const auto magnitude = [](i64 v) { return static_cast<std::uint64_t>(v < 0 ? -v : v); };
    const BigUint left = big_mul(big_mul(big_mul(big_from(355), big_from(static_cast<std::uint64_t>(frames))),
                                         big_mul(big_from(magnitude(x.num)), big_from(magnitude(y.num)))),
                                 big_mul(big_from(static_cast<std::uint64_t>(m.den)), big_from(static_cast<std::uint64_t>(m.den))));
    const BigUint right = big_mul(big_mul(big_from(113), big_mul(big_from(magnitude(m.num)), big_from(magnitude(m.num)))),
                                  big_mul(big_from(static_cast<std::uint64_t>(x.den)), big_from(static_cast<std::uint64_t>(y.den))));
    return big_cmp(left, right) > 0;
}

// fmt() of the Python tool: value rounded to kFrameCountMaxPlaces decimals (halves away from zero), then trailing zeros
// removed down to min_places decimals (the point too, when no decimal is left). 30 gives "30"; 2 with min_places 2 gives "2.00".
std::string frame_count_text(const Rational& value, std::size_t min_places) {
    const std::string text = format_fixed(quantize_scaled(value, kFrameCountMaxPlaces), kFrameCountMaxPlaces);
    const std::size_t dot = text.find('.');
    std::size_t end = text.size();
    while (end > dot + 1 + min_places && text[end - 1] == '0') {
        --end;
    }
    if (end == dot + 1) {
        end = dot;
    }
    return text.substr(0, end);
}

// Phase 2 for the rows of followed scenes with k != 1, in file order. A row that overlaps a 時間制御(オブジェクト) object of
// its scene (frame ranges, any layer) is refused. 反復移動 and ランダム移動: the parameter times k (exact); 移動量指定: the step
// divided by k. ランダム移動 is refused for an object that does not start at frame 0. Each rewrite is checked against the error
// bound E = |e - s| x PI_UPPER x F x |Q - P2| / (2 x min(Q, P2)^2) (F = frames of the new object minus 1; for 移動量指定,
// the step is written with the decimals of its second value and E = |Q - D2| x F). Returns the new rows by line index and
// adds one note per row that changes.
std::map<std::size_t, std::string> plan_frame_modes(const std::vector<std::string>& lines,
                                                    const std::vector<FrameModeRow>& rows,
                                                    const std::vector<TimeControl>& time_controls,
                                                    const std::map<i64, std::size_t>& frame_line_of,
                                                    const std::map<i64, i64>& object_scene,
                                                    const std::map<i64, std::vector<i64>>& frame_values,
                                                    const std::map<i64, std::vector<i64>>& new_frames,
                                                    const std::map<i64, Rational>& k_of, RetimeResult& res) {
    std::map<std::size_t, std::string> bodies;
    for (const FrameModeRow& row : rows) {
        const Rational& k = k_of.at(row.obj);
        if (rat_equal(k, Rational{1, 1})) {
            continue;
        }
        const std::string line = std::to_string(row.idx + 1);
        const std::vector<i64>& old = frame_values.at(row.obj);
        const std::vector<i64>& nw = new_frames.at(row.obj);

        std::optional<std::size_t> overlap;  // the frame= line of the first time control object that overlaps, if any
        for (const TimeControl& tc : time_controls) {
            if (object_scene.at(tc.obj) != object_scene.at(row.obj)) {
                continue;
            }
            const std::vector<i64>& tc_frames = frame_values.at(tc.obj);
            if (tc_frames.back() < old.front() || old.back() < tc_frames.front()) {
                continue;
            }
            const std::size_t tc_line = frame_line_of.at(tc.obj) + 1;
            if (!overlap || tc_line < *overlap) {
                overlap = tc_line;
            }
        }
        if (overlap) {
            refuse(row.idx + 1, row.mode + " is not verified together with " + kTimeControlEffect +
                                    " (the time control object at line " + std::to_string(*overlap) + " overlaps it)");
        }
        if (row.mode == kFrameCountRandom && old.front() != 0) {
            refuse(row.idx + 1, row.mode +
                                    " is accepted only for objects that start at frame 0 (its random path depends on the start frame)");
        }

        const i64 frames_new = checked_sub(nw.back(), nw.front());
        const std::string key = key_of(lines[row.idx]);
        std::size_t digits = 0;
        std::string new_body;
        std::string note;
        if (row.mode == kFrameCountStep) {
            std::size_t step_places = 0;  // the decimals of the second value as written: the written step has exactly that many
            const Rational step2 = rat_div(parse_decimal(row.values[1], step_places), k);
            const std::string text = format_fixed(quantize_scaled(step2, step_places), step_places);
            const Rational written = parse_decimal(text, digits);
            const Rational error = rat_mul(rat_abs(rat_sub(written, step2)), Rational{frames_new, 1});
            if (rat_less(kFrameCountTolerance, error)) {
                refuse(row.idx + 1, row.mode + " step " + row.values[1] + " cannot be written with " +
                                        std::to_string(step_places) + " decimals within 0.5");
            }
            new_body = key + "=" + row.values[0] + "," + text + "," + row.mode + "," + kFrameCountFlag;
            note = "trackbar at line " + line + ": " + row.mode + " step " + row.values[1] + " -> " + text;
        } else {
            const Rational param2 = rat_mul(parse_decimal(row.payload, digits), k);
            if (row.mode == kFrameCountRandom && param2.den != 1) {
                refuse(row.idx + 1, row.mode + " parameter " + row.payload + " becomes " + fraction_text(param2) +
                                        " at the target rate; a non-integer parameter is not verified");
            }
            const std::string text = frame_count_text(param2, 0);
            const Rational written = parse_decimal(text, digits);
            const Rational s = parse_decimal(row.values[0], digits);
            const Rational e = parse_decimal(row.values[1], digits);
            const Rational smaller = rat_less(written, param2) ? written : param2;
            bool exceeds = smaller.num == 0;  // the bound is unbounded when the value rounds to zero
            if (!exceeds) {
                exceeds = error_exceeds_half(rat_abs(rat_sub(e, s)), frames_new, rat_abs(rat_sub(written, param2)), smaller);
            }
            if (exceeds) {
                refuse(row.idx + 1, row.mode + " parameter " + row.payload + " cannot be written with " +
                                        std::to_string(kFrameCountMaxPlaces) + " decimals within 0.5");
            }
            new_body = key + "=" + row.values[0] + "," + row.values[1] + "," + row.mode + "," + kFrameCountFlag + "|" + text;
            note = "trackbar at line " + line + ": " + row.mode + " parameter " + row.payload + " -> " + text;
        }
        if (new_body != lines[row.idx]) {
            bodies[row.idx] = new_body;
            res.notes.push_back(note);
        }
    }
    return bodies;
}

// ---------------------------------------------------------------------------
// Scene alignment. A sub scene that an シーン object plays starts at that object's first frame S, but its frames are rounded
// from its own origin. At a rate where the origin times k is not an integer, its boundaries can fall one frame away from
// the exported scene's (the sub scene shows its new content a frame before a filter of the main scene starts). A scene that
// can be aligned is rescaled on the timeline of its player: scene frame f is exported frame f + D, with D = S - P (P: the
// player's start position in frames of the scene), and its new frame is rescale(f + D) - Dn, with Dn = round(S k) - P k.
// Any rule that fails leaves the scene on its own origin and says why in a WARNING note.
// ---------------------------------------------------------------------------

// The offsets of one aligned scene (see above).
struct SceneAlignment {
    i64 shift = 0;                // D
    i64 new_shift = 0;            // Dn
    std::size_t player_line = 0;  // frame= line of the player object (1-based)
    bool fractional = false;      // S k is not an integer: the scene is rescaled on the player's timeline (note)
};

// One シーン effect that names a scene: its object and its keys (the effect's own key-value map).
struct ScenePlayer {
    i64 obj = 0;
    const std::map<std::string, Prop>* props = nullptr;
};

// A scene number written as digits. Empty when it does not fit in 64 bits: no scene has such a number, so the effect plays
// no scene (not an error).
std::optional<i64> scene_number_of(const std::string& digits) {
    i64 value = 0;
    try {
        for (char c : digits) {
            value = checked_add(checked_mul(value, 10), c - '0');
        }
    } catch (const Overflow&) {
        return std::nullopt;
    }
    return value;
}

// The rules for the one player of a scene, in order. Returns the reason the scene cannot be aligned, or an empty string when
// it can; al is filled then.
std::string player_problem(const ScenePlayer& player, const SceneRate& sr, i64 scene_no, i64 export_scene,
                           const std::map<std::pair<i64, i64>, std::map<std::string, Prop>>& effect_props,
                           const std::map<i64, i64>& object_scene, const std::map<i64, std::vector<i64>>& frame_values,
                           const std::map<i64, std::size_t>& frame_line_of, const Rational& k, SceneAlignment& al) {
    const std::map<std::string, Prop>& props = *player.props;
    const auto prop_text = [&props](const std::string& key, const std::string& fallback) {
        const auto found = props.find(key);
        return found == props.end() ? fallback : found->second.value;
    };

    // The player is an object of the exported scene.
    const auto owner = object_scene.find(player.obj);
    if (owner == object_scene.end() || owner->second != export_scene) {
        return "its scene object is not in the exported scene";
    }
    const auto frames = frame_values.find(player.obj);
    if (frames == frame_values.end() || frames->second.size() != 2) {
        return "its scene object has more than two frame points";
    }
    // Plays at speed 100 without loop (both default when absent).
    std::size_t digits = 0;
    const std::string speed = prop_text(kScenePlaySpeed, "100");
    if (!is_number(speed) || !rat_equal(parse_decimal(speed, digits), Rational{100, 1}) ||
        prop_text(kScenePlayLoop, "0") != "0") {
        return "its scene object does not play at speed 100 without loop";
    }
    // The start position: a plain number, or a 再生範囲 trackbar with two values (its first value). The start must be a whole
    // frame of the scene at both rates.
    Rational origin{0, 1};  // seconds
    TrackText track;
    const std::string position = prop_text(kScenePlayPosition, "0");
    if (parse_track_text(position, track)) {
        if (track.mode != kPlayRangeMode || track.values.size() != 2) {
            return "its scene object starts at a position that is not a whole frame at both rates";
        }
        origin = parse_decimal(track.values[0], digits);
    } else if (is_number(position)) {
        origin = parse_decimal(position, digits);
    } else {
        return "its scene object starts at a position that is not a whole frame at both rates";
    }
    const Rational start = rat_mul(origin, sr.rate);   // P: the start in frames of the scene
    const Rational start_scaled = rat_mul(start, k);   // P k
    if (rat_less(origin, Rational{0, 1}) || start.den != 1 || start_scaled.den != 1) {  // a negative start is rule 5 too
        return "its scene object starts at a position that is not a whole frame at both rates";
    }
    // A time control object in the scene would be rescaled on its own timeline (its 位置 is worked out from the frames).
    for (const auto& [key, eff] : effect_props) {
        const auto name = eff.find("effect.name");
        const auto in_scene = object_scene.find(key.first);
        if (name != eff.end() && name->second.value == kTimeControlEffect && in_scene != object_scene.end() &&
            in_scene->second == scene_no) {
            return "it contains a time control object";
        }
    }

    const i64 first = frames->second.front();  // S
    al.shift = checked_sub(first, start.num);
    al.new_shift = checked_sub(round_scaled(first, k), start_scaled.num);
    al.player_line = frame_line_of.at(player.obj) + 1;
    al.fractional = rat_mul(Rational{first, 1}, k).den != 1;
    return std::string();
}

// The aligned scenes (scene number -> offsets) of the followed scenes at the exported scene's k. A scene is visited only when
// its k is that of the exported scene and it has at least one player; the notes are appended here, in scene-number order: a
// WARNING for each scene that cannot be aligned (also in res.warnings, as in the Python tool's warnings list), and a note
// for each aligned scene whose origin times k is not an integer. Empty when k is an integer.
std::map<i64, SceneAlignment> plan_scene_alignment(
    const std::map<std::pair<i64, i64>, std::map<std::string, Prop>>& effect_props,
    const std::map<i64, i64>& object_scene, const std::map<i64, SceneRate>& scene_rate_lines, i64 export_scene,
    const Rational& k, const std::map<i64, std::vector<i64>>& frame_values,
    const std::map<i64, std::size_t>& frame_line_of, RetimeResult& res) {
    std::map<i64, SceneAlignment> aligned;
    if (k.den == 1) {
        return aligned;
    }

    // Every シーン effect that names a scene number, by that number (a number that does not fit names no scene).
    std::map<i64, std::vector<ScenePlayer>> players_of;
    for (const auto& [owner, props] : effect_props) {
        const auto name = props.find("effect.name");
        const auto target = props.find(kScenePlayerKey);
        if (name == props.end() || name->second.value != kScenePlayerEffect || target == props.end() ||
            !all_digits(target->second.value)) {
            continue;
        }
        if (const std::optional<i64> scene = scene_number_of(target->second.value)) {
            players_of[*scene].push_back(ScenePlayer{owner.first, &props});
        }
    }

    for (const auto& [scene_no, sr] : scene_rate_lines) {
        if (scene_no == export_scene || !sr.follow || !rat_equal(sr.k, k)) {
            continue;
        }
        const auto found = players_of.find(scene_no);
        if (found == players_of.end()) {
            continue;
        }
        const std::string no = std::to_string(scene_no);
        SceneAlignment al;
        std::string problem;
        if (found->second.size() != 1) {
            problem = "it is played by " + std::to_string(found->second.size()) + " scene objects";
        } else {
            problem = player_problem(found->second[0], sr, scene_no, export_scene, effect_props, object_scene,
                                     frame_values, frame_line_of, k, al);
        }
        if (!problem.empty()) {
            const std::string warning = "WARNING: scene " + no + " cannot be aligned to the exported scene (" + problem +
                                        "); object boundaries inside it may be one frame off at this rate";
            res.notes.push_back(warning);
            res.warnings.push_back(warning);
            continue;
        }
        aligned[scene_no] = al;
        if (al.fractional) {
            res.notes.push_back("scene " + no + " is rescaled on the timeline of its scene object at line " +
                                std::to_string(al.player_line));
        }
    }
    return aligned;
}

// Python convert(). Writes into res and returns the output bytes. Throws Refused or Overflow.
std::string convert(std::string_view data, const Mode& mode, const std::string& out_utf8, RetimeResult& res) {
    if (mode.factor && mode.factor_k < 2) {
        refuse(0, "factor must be an integer >= 2, got " + std::to_string(mode.factor_k));
    }

    static const std::string_view kBom = "\xEF\xBB\xBF";
    const bool has_bom = data.substr(0, kBom.size()) == kBom;
    const std::string_view body_bytes = has_bom ? data.substr(kBom.size()) : data;
    if (!valid_utf8(body_bytes)) {
        refuse(0, "input is not valid UTF-8");
    }
    const std::vector<Record> records = split_records(body_bytes);
    std::vector<std::string> lines;
    lines.reserve(records.size());
    for (const auto& rec : records) {
        lines.push_back(rec.body);
    }

    // Pass 1: map each line to its section, collect frame=, layer=, scene= and effect lines.
    std::vector<Section> section_of(lines.size());
    Section section;
    std::vector<std::size_t> project_file_lines;
    std::optional<std::pair<std::string, std::size_t>> display_scene;  // [project] display.scene= (the first one)
    // Lines with コマ落ち other than 1, and their sections. The check waits for the scene plan: a line inside a kept
    // scene is left alone.
    std::vector<std::pair<std::size_t, Section>> column_lines;
    bool project_seen = false;
    std::map<i64, std::vector<std::size_t>> section_lines;  // by section number N; re-keyed by scene ID below
    std::vector<i64> scene_order;
    std::vector<std::pair<i64, std::size_t>> frame_lines;  // (object, line index), file order
    std::map<i64, std::size_t> frame_line_of;
    std::map<i64, std::string> object_layer;
    std::map<i64, std::pair<std::string, std::size_t>> object_scene_lines;
    std::map<i64, std::optional<i64>> obj_header_scene;  // object -> last [scene.N] above its header
    std::vector<i64> obj_header_order;
    std::optional<i64> header_scene;
    std::vector<std::size_t> effect_lines;
    std::map<std::pair<i64, i64>, std::map<std::string, Prop>> effect_props;

    for (std::size_t idx = 0; idx < lines.size(); ++idx) {
        const std::string& body = lines[idx];
        if (body.size() >= 2 && body.front() == '[' && body.back() == ']') {
            if (body == "[project]") {
                project_seen = true;
            }
            std::string d1;
            std::string d2;
            if (scene_header(body, d1)) {
                const i64 n = parse_digits(d1, idx + 1, "scene number");
                if (section_lines.find(n) == section_lines.end()) {
                    section_lines[n];
                    scene_order.push_back(n);
                }
                header_scene = n;
                section = Section{Kind::kScene, n, 0};
            } else if (object_header(body, d1)) {
                const i64 n = parse_digits(d1, idx + 1, "object number");
                if (obj_header_scene.find(n) == obj_header_scene.end()) {
                    obj_header_order.push_back(n);
                }
                obj_header_scene[n] = header_scene;
                section = Section{Kind::kObject, n, 0};
            } else if (effect_header(body, d1, d2)) {
                section = Section{Kind::kEffect, parse_digits(d1, idx + 1, "object number"),
                                  parse_digits(d2, idx + 1, "effect number")};
            } else if (body == "[project]") {
                section = Section{Kind::kProject, 0, 0};
            } else {
                section = Section{Kind::kOther, 0, 0};
            }
            section_of[idx] = section;
            continue;
        }
        section_of[idx] = section;
        if (section.kind == Kind::kNone || body.find('=') == std::string::npos) {
            continue;
        }
        const std::string key = key_of(body);
        const std::string value = value_of(body);
        switch (section.kind) {
        case Kind::kProject:
            if (key == "file") {
                project_file_lines.push_back(idx);
            } else if (key == "display.scene" && !display_scene) {
                display_scene = std::make_pair(value, idx);
            }
            break;
        case Kind::kScene:
            section_lines[section.a].push_back(idx);
            break;
        case Kind::kObject:
            if (key == "frame") {
                if (frame_line_of.find(section.a) != frame_line_of.end()) {
                    refuse(idx + 1, "duplicate frame= in object [" + std::to_string(section.a) + "]");
                }
                frame_line_of[section.a] = idx;
                frame_lines.emplace_back(section.a, idx);
            } else if (key == "layer") {
                object_layer[section.a] = value;
            } else if (key == "scene") {
                object_scene_lines[section.a] = std::make_pair(value, idx);
            }
            break;
        case Kind::kEffect:
            effect_lines.push_back(idx);
            effect_props[std::make_pair(section.a, section.b)][key] = Prop{value, idx};
            break;
        default:
            break;
        }
        if (key == kColumnKey && value != kColumnNoDrop) {
            column_lines.emplace_back(idx, section);
        }
    }

    if (!project_seen || project_file_lines.size() != 1) {
        refuse(0, "expected exactly one 'file=' line in [project]");
    }
    if (section_lines.empty()) {
        refuse(0, "no [scene.N] section found");
    }

    // Scene IDs. The ID of a [scene.N] section is the value of its first scene= line (an integer), or N when it has none.
    // Objects and シーン effects name scenes by ID, so every table of scenes is keyed by ID. Each ID belongs to one section.
    std::map<i64, i64> section_id;  // section number N -> scene ID
    std::map<i64, i64> id_section;  // scene ID -> section number N
    std::set<i64> scene_ids_seen;
    // The sections in ascending section number: the first section with a bad or repeated ID is the one reported.
    std::vector<i64> sections_ascending = scene_order;
    std::sort(sections_ascending.begin(), sections_ascending.end());
    for (i64 section_no : sections_ascending) {
        i64 id = section_no;
        std::optional<std::size_t> id_line;  // the scene= line that gives the ID, when there is one
        for (std::size_t idx : section_lines[section_no]) {
            if (key_of(lines[idx]) == "scene") {
                const std::string value = value_of(lines[idx]);
                if (!all_digits(value)) {
                    refuse(idx + 1, "scene= in [scene." + std::to_string(section_no) +
                                        "] must be a non-negative integer, got '" + value + "'");
                }
                id = parse_digits(value, idx + 1, "scene=");
                id_line = idx;
                break;
            }
        }
        if (!scene_ids_seen.insert(id).second) {
            refuse(id_line ? *id_line + 1 : 0,
                   "scene id " + std::to_string(id) + " is used by more than one [scene.N] section");
        }
        section_id[section_no] = id;
        id_section[id] = section_no;
    }
    // The lines of each scene, by scene ID (a section's lines go to its ID).
    std::map<i64, std::vector<std::size_t>> scene_lines;
    for (i64 section_no : scene_order) {
        scene_lines[section_id.at(section_no)] = section_lines[section_no];
    }

    // Each object belongs to one scene: its scene= line if it has one, else the [scene.N] section above it (its ID),
    // else scene 0.
    std::map<i64, i64> object_scene;
    for (i64 obj : obj_header_order) {
        const auto sl = object_scene_lines.find(obj);
        if (sl != object_scene_lines.end()) {
            if (!all_digits(sl->second.first)) {
                refuse(sl->second.second + 1, "object scene= must be an integer, got '" + sl->second.first + "'");
            }
            object_scene[obj] = parse_digits(sl->second.first, sl->second.second + 1, "object scene=");
        } else {
            const std::optional<i64> header = obj_header_scene[obj];
            object_scene[obj] = header ? section_id.at(*header) : 0;
        }
    }

    // Pass 2: scene checks. Scenes may have different rates; the exported scene (display.scene) decides the
    // target rate, and each other scene follows it or is kept as it is (subscene_rate).
    res.subscene_rate = policy_name(mode.subscene_rate);
    std::map<i64, SceneRate> scene_rate_lines;  // by scene ID
    for (const auto& [scene_id, idxs] : scene_lines) {  // ascending by scene ID
        const i64 section_no = id_section.at(scene_id);
        std::optional<std::pair<i64, std::size_t>> rate_v;
        std::optional<std::pair<i64, std::size_t>> scale_v;
        std::string scene_name;  // the first name= line
        bool name_seen = false;
        for (std::size_t idx : idxs) {
            const std::string key = key_of(lines[idx]);
            const std::string value = value_of(lines[idx]);
            if (key == "video.rate" || key == "video.scale") {
                if (!all_digits(value) || parse_digits(value, idx + 1, key) == 0) {
                    refuse(idx + 1, key + " must be a positive integer, got '" + value + "'");
                }
                auto& slot = key == "video.rate" ? rate_v : scale_v;
                slot = std::make_pair(parse_digits(value, idx + 1, key), idx);
            } else if (kSceneKnownKeys.count(key) != 0) {
                if (kSceneFrameKeys.count(key) != 0 && !all_digits(value)) {
                    refuse(idx + 1, key + " must be an integer, got '" + value + "'");
                }
                if (key == "name" && !name_seen) {
                    scene_name = value;
                    name_seen = true;
                }
            } else {
                for (const auto& marker : kSceneRefuseMarkers) {
                    if (key.find(marker) != std::string::npos) {
                        refuse(idx + 1, "unknown scene key '" + key + "' may hold frame/mark/range data");
                    }
                }
            }
        }
        if (!rate_v) {
            refuse(0, "[scene." + std::to_string(section_no) + "] has no video.rate");
        }
        i64 scale_value = 1;
        std::optional<std::size_t> scale_idx;
        if (scale_v) {
            scale_value = scale_v->first;
            scale_idx = scale_v->second;
        }
        SceneRate sr;
        sr.rate_idx = rate_v->second;
        sr.scale_idx = scale_idx;
        sr.rate_value = rate_v->first;
        sr.scale_value = scale_value;
        sr.rate = make_rational(sr.rate_value, sr.scale_value);
        sr.name = scene_name;
        scene_rate_lines[scene_id] = sr;
    }

    // The exported scene: display.scene, 0 when absent.
    i64 export_scene = 0;
    if (display_scene) {
        if (!all_digits(display_scene->first)) {
            refuse(display_scene->second + 1,
                   "display.scene must be a non-negative integer, got '" + display_scene->first + "'");
        }
        export_scene = parse_digits(display_scene->first, display_scene->second + 1, "display.scene");
    }
    const auto export_found = scene_rate_lines.find(export_scene);
    if (export_found == scene_rate_lines.end()) {
        refuse(0, "display.scene " + std::to_string(export_scene) + " does not exist");
    }
    // display.scene names an ID. A [scene.V] section with another ID makes the number ambiguous (only when the row exists).
    const auto same_number = section_id.find(export_scene);
    if (display_scene && same_number != section_id.end() && same_number->second != export_scene) {
        refuse(display_scene->second + 1,
               "display.scene " + std::to_string(export_scene) + " is ambiguous: [scene." +
                   std::to_string(export_scene) + "] has scene=" + std::to_string(same_number->second));
    }
    const SceneRate& export_rate = export_found->second;
    const i64 src_rate = export_rate.rate_value;
    const i64 src_scale = export_rate.scale_value;

    // Target rate: factor mode keeps video.scale (not reduced); target mode writes the reduced fraction.
    i64 target_num = mode.target.num;
    i64 target_den = mode.target.den;
    if (mode.factor) {
        target_num = checked_mul(src_rate, mode.factor_k);
        target_den = src_scale;
    }
    const Rational rate_src = export_rate.rate;
    const Rational rate_dst = make_rational(target_num, target_den);
    const Rational k = rat_div(rate_dst, rate_src);

    // Which scenes follow, and by how much each is re-timed. The exported scene always follows.
    for (auto& [scene_no, sr] : scene_rate_lines) {
        switch (mode.subscene_rate) {
        case SubsceneRatePolicy::Follow:
            sr.follow = true;
            break;
        case SubsceneRatePolicy::Keep:
            sr.follow = scene_no == export_scene;
            break;
        case SubsceneRatePolicy::Auto:
            sr.follow = scene_no == export_scene || !rat_less(sr.rate, rate_src);
            break;
        }
        sr.k = sr.follow ? rat_div(rate_dst, sr.rate) : Rational{1, 1};
    }
    // The plan and its notes, by scene number.
    for (const auto& [scene_no, sr] : scene_rate_lines) {
        ScenePlan plan;
        plan.scene = static_cast<int>(scene_no);
        plan.name = sr.name;
        plan.source_rate = fraction_text(sr.rate);
        plan.target_rate = sr.follow ? fraction_text(rate_dst) : plan.source_rate;
        plan.k = fraction_text(sr.k);
        plan.follow = sr.follow;
        plan.exported = scene_no == export_scene;
        res.scenes.push_back(plan);
        if (!sr.follow) {
            res.notes.push_back("scene " + std::to_string(scene_no) + " kept at its own rate " + plan.source_rate);
        } else if (!sr.scale_idx) {
            res.notes.push_back("[scene." + std::to_string(id_section.at(scene_no)) +
                                "] has no video.scale; read as 1 and written after video.rate");
        }
    }

    // The scene an object belongs to (an object on a scene that has no section follows the exported scene).
    auto scene_of_object = [&](i64 obj) -> const SceneRate& {
        const auto owner = object_scene.find(obj);
        if (owner != object_scene.end()) {
            const auto found = scene_rate_lines.find(owner->second);
            if (found != scene_rate_lines.end()) {
                return found->second;
            }
        }
        return export_rate;
    };

    // コマ落ち: 1 stays. K >= 2 is converted (plan_time_control) inside a time control effect; any other value, and
    // K >= 2 elsewhere, is refused. Lines of kept scenes are not looked at; lines outside objects, effects and scenes
    // are (the exported scene always follows). The first line in file order that is refused is reported.
    auto is_time_control_effect = [&](const Section& sec) {
        if (sec.kind != Kind::kEffect) {
            return false;
        }
        const auto& props = effect_props.at(std::make_pair(sec.a, sec.b));
        const auto name = props.find("effect.name");
        return name != props.end() && name->second.value == kTimeControlEffect;
    };
    for (const auto& [idx, sec] : column_lines) {
        const SceneRate* owner = &export_rate;
        if (sec.kind == Kind::kScene) {
            owner = &scene_rate_lines.at(section_id.at(sec.a));
        } else if (sec.kind == Kind::kObject || sec.kind == Kind::kEffect) {
            owner = &scene_of_object(sec.a);
        }
        if (!owner->follow) {
            continue;
        }
        const std::string value = value_of(lines[idx]);
        if (!all_digits(value) || parse_digits(value, idx + 1, kColumnKey + "= value") == 0) {
            refuse(idx + 1, kColumnKey + "=" + value + " (frame drop) must be a positive integer");
        }
        const i64 drop = parse_digits(value, idx + 1, kColumnKey + "= value");
        if (drop == 1) {
            continue;
        }
        if (!is_time_control_effect(sec)) {
            refuse(idx + 1, kColumnKey + "=" + value + " (frame drop) is only converted inside a " +
                                kTimeControlEffect + " effect");
        }
    }

    // Pass 3: object frame lists, and trackbars that depend on them. Objects of a kept scene are not looked at.
    std::map<i64, std::vector<i64>> frame_values;
    for (const auto& [obj, idx] : frame_lines) {
        if (scene_of_object(obj).follow) {
            frame_values[obj] = parse_frames(value_of(lines[idx]), idx + 1);
        }
    }
    // Scene alignment (see plan_scene_alignment): its notes come after the scene notes and before the object notes.
    const std::map<i64, SceneAlignment> alignments = plan_scene_alignment(
        effect_props, object_scene, scene_rate_lines, export_scene, k, frame_values, frame_line_of, res);
    // The alignment of the scene an object belongs to, or null when that scene is not aligned.
    const auto alignment_of = [&](i64 obj) -> const SceneAlignment* {
        const auto owner = object_scene.find(obj);
        if (owner == object_scene.end()) {
            return nullptr;
        }
        const auto found = alignments.find(owner->second);
        return found == alignments.end() ? nullptr : &found->second;
    };
    std::vector<std::size_t> counter_lines;
    for (const auto& entry : effect_props) {
        const auto& props = entry.second;
        const auto name = props.find("effect.name");
        if (name == props.end() || name->second.value != kCounterEffect || !scene_of_object(entry.first.first).follow) {
            continue;
        }
        const auto speed = props.find(kSpeedKey);
        if (speed == props.end()) {
            refuse(name->second.idx + 1, kCounterEffect + " has no " + kSpeedKey + "= line");
        }
        if (!is_number(speed->second.value)) {
            refuse(speed->second.idx + 1, kCounterEffect + " " + kSpeedKey + " must be a plain number, got '" +
                                              speed->second.value + "'");
        }
        counter_lines.push_back(name->second.idx + 1);
    }
    if (!counter_lines.empty()) {
        std::string list;
        for (std::size_t i = 0; i < counter_lines.size(); ++i) {
            if (i > 0) {
                list += ", ";
            }
            list += std::to_string(counter_lines[i]);
        }
        res.notes.push_back(kCounterEffect + ": time-based (" + kSpeedKey +
                            " = counts per second), passed through unchanged (lines " + list + ")");
    }

    // 時間制御(オブジェクト) of followed scenes: 位置 must be exactly two numbers, the time control mode and a flag
    // without '|', on an object with two frame= points (a missing 位置 is reported at the effect.name line). Effects
    // are visited in (object, effect) order; the last 位置 line of an effect counts.
    std::vector<TimeControl> time_controls;
    std::set<std::size_t> time_control_positions;
    std::set<std::pair<i64, i64>> time_control_sections;  // every time control effect, also in kept scenes
    for (const auto& entry : effect_props) {
        const auto name = entry.second.find("effect.name");
        if (name == entry.second.end() || name->second.value != kTimeControlEffect) {
            continue;
        }
        time_control_sections.insert(entry.first);
        if (!scene_of_object(entry.first.first).follow) {
            continue;
        }
        const auto position = entry.second.find(kTimeControlPosition);
        const std::size_t refusal_idx = position == entry.second.end() ? name->second.idx : position->second.idx;
        const auto fv = frame_values.find(entry.first.first);
        TrackText pos;
        if (position == entry.second.end() || !parse_track_text(position->second.value, pos) ||
            pos.values.size() != 2 || pos.mode != kTimeControlMode || fv == frame_values.end() ||
            fv->second.size() != 2) {
            refuse(refusal_idx + 1, kTimeControlRefusal);
        }
        TimeControl tc;
        tc.obj = entry.first.first;
        tc.effect = entry.first.second;
        tc.pos_idx = position->second.idx;
        tc.a_text = pos.values[0];
        tc.b_text = pos.values[1];
        tc.flag = pos.param;
        tc.curve = pos.payload.has_value();
        time_controls.push_back(tc);
        time_control_positions.insert(tc.pos_idx);
    }

    std::vector<FrameModeRow> frame_mode_rows;  // frame-counting rows of followed objects, file order
    for (std::size_t idx : effect_lines) {
        const Section sec = section_of[idx];
        if (!scene_of_object(sec.a).follow) {
            continue;
        }
        const std::string key = key_of(lines[idx]);
        const std::string value = value_of(lines[idx]);
        const auto& props = effect_props.at(std::make_pair(sec.a, sec.b));
        std::optional<std::string> effect_name;
        const auto name = props.find("effect.name");
        if (name != props.end()) {
            effect_name = name->second.value;
        }
        if (key == "effect.name" && kFrameEffectNames.count(value) != 0 &&
            !rat_equal(scene_of_object(sec.a).k, Rational{1, 1})) {
            refuse(idx + 1, "effect '" + value +
                                "' depends on the frame number or a per-frame random value and is not verified at another "
                                "frame rate; refused");
        }
        if (key == "effect.name" && value == kFlareEffectName && !rat_equal(scene_of_object(sec.a).k, Rational{1, 1})) {
            const auto blink = props.find(kFlareBlinkKey);
            if (blink == props.end() || !is_zero_number(blink->second.value)) {
                refuse(idx + 1, "effect '" + kFlareEffectName + "' with " + kFlareBlinkKey + "=" +
                                    (blink == props.end() ? std::string("(missing)") : blink->second.value) +
                                    " is not verified at another frame rate (only " + kFlareBlinkKey + "=0 is); refused");
            }
        }
        std::string marker;
        if (find_marker(decode_utf8(script_text(effect_name, key, value)), marker)) {
            refuse(idx + 1, "script text uses frame-dependent marker '" + marker + "' in " +
                                effect_name.value_or(std::string("None")) + " effect (" + key +
                                "); refused (pass --allow-frame-scripts to convert anyway)");
        }
        if (time_control_positions.count(idx) != 0) {
            continue;  // the validated 位置 row of a time control effect: rewritten below
        }
        TrackText text;
        if (!parse_track_text(value, text)) {
            continue;
        }
        const std::string& mode_name = text.mode;
        const i64 obj = sec.a;
        const auto fv = frame_values.find(obj);
        if (kFrameCountModes.count(mode_name) != 0) {
            // The frame-counting modes have their own rules in place of the whitelist and the payload check.
            const std::string payload = check_frame_mode(idx + 1, text, fv == frame_values.end() ? nullptr : &fv->second);
            frame_mode_rows.push_back(FrameModeRow{idx, obj, mode_name, text.values, payload});
        } else {
            if (kTrackModes.count(mode_name) == 0) {
                refuse(idx + 1, "trackbar mode '" + mode_name + "' is not on the whitelist");
            }
            if (text.payload && kTrackPayloadModes.count(mode_name) == 0) {
                refuse(idx + 1, "trackbar '|' payload '" + *text.payload + "' is not on the whitelist");
            }
        }
        if (fv != frame_values.end() && kTrackCountExempt.count(mode_name) == 0) {
            const std::size_t count = text.values.size();
            if (count != fv->second.size()) {
                refuse(idx + 1, "trackbar has " + std::to_string(count) + " values but object [" +
                                    std::to_string(obj) + "] has " + std::to_string(fv->second.size()) +
                                    " frame points");
            }
        }
    }
    // Rescale every object of a followed scene (each by its own scene's k), then check the layers. Objects of a
    // kept scene are not touched and are not part of the layer check, the timing report or precise mode.
    std::vector<std::pair<i64, std::size_t>> followed_frame_lines;  // (object, line index), file order
    std::map<i64, Rational> object_k;
    std::map<i64, std::vector<i64>> new_frames;
    std::set<std::size_t> shortened;
    for (const auto& entry : frame_lines) {
        const i64 obj = entry.first;
        const std::size_t idx = entry.second;
        const SceneRate& owner = scene_of_object(obj);
        if (!owner.follow) {
            continue;
        }
        followed_frame_lines.push_back(entry);
        object_k[obj] = owner.k;
        bool was_short = false;
        if (const SceneAlignment* al = alignment_of(obj)) {
            // The unshifted rescale runs first, so an object refused for its own frames gets the same text as before.
            bool unshifted_short = false;
            rescale_frames(frame_values[obj], owner.k, idx + 1, unshifted_short);
            std::vector<i64> shifted;
            for (i64 v : frame_values[obj]) {
                shifted.push_back(checked_add(v, al->shift));
            }
            try {
                new_frames[obj] = rescale_frames(shifted, owner.k, idx + 1, was_short);
            } catch (const Refused&) {
                refuse(idx + 1, "frame= points collide or stop increasing after rescaling on the timeline of the scene "
                                "object at line " + std::to_string(al->player_line) + "; refused");
            }
            for (i64& f : new_frames[obj]) {
                f = checked_sub(f, al->new_shift);
            }
        } else {
            new_frames[obj] = rescale_frames(frame_values[obj], owner.k, idx + 1, was_short);
        }
        if (was_short) {
            shortened.insert(idx + 1);
            res.notes.push_back("object at line " + std::to_string(idx + 1) + " shortened to 1 frame");
        }
    }
    check_layers(followed_frame_lines, object_layer, object_scene, frame_values, new_frames);

    // The frame-counting rows (their notes come before the time control notes, as in the Python tool).
    const std::map<std::size_t, std::string> frame_mode_bodies = plan_frame_modes(
        lines, frame_mode_rows, time_controls, frame_line_of, object_scene, frame_values, new_frames, object_k, res);

    // The rows of 時間制御 and, in precise mode, of the trackbars are worked out before anything is written: both can give
    // an object a longer frame= list (written_frames). The notes of the time control come before the precise notes.
    std::map<i64, std::vector<i64>> written_frames = new_frames;
    const std::map<std::size_t, std::string> time_control_bodies =
        plan_time_control(lines, time_controls, effect_lines, section_of, effect_props, frame_line_of, frame_values,
                          new_frames, object_k, written_frames, res);
    std::map<std::size_t, std::string> precise_bodies;
    if (mode.precise) {
        // An aligned scene is re-sampled on its player's timeline: the copies hold its frames at the exported positions
        // (+D) and its new frames before the shift back (+Dn). The frames written are shifted back by Dn.
        std::map<i64, std::vector<i64>> aligned_values = frame_values;
        std::map<i64, std::vector<i64>> aligned_new = new_frames;
        for (const auto& entry : followed_frame_lines) {
            if (const SceneAlignment* al = alignment_of(entry.first)) {
                for (i64& v : aligned_values[entry.first]) {
                    v = checked_add(v, al->shift);
                }
                for (i64& f : aligned_new[entry.first]) {
                    f = checked_add(f, al->new_shift);
                }
            }
        }
        std::map<i64, std::vector<i64>> precise_frames;
        precise_bodies = apply_precise(lines, effect_lines, section_of, followed_frame_lines, aligned_values, aligned_new,
                                       object_k, time_control_sections, mode.precise_extra_decimals, precise_frames, res);
        for (auto& [obj, frames] : precise_frames) {
            if (const SceneAlignment* al = alignment_of(obj)) {
                for (i64& f : frames) {
                    f = checked_sub(f, al->new_shift);
                }
            }
            written_frames[obj] = frames;
        }
    }

    // Pass 4: build the rewritten lines.
    std::vector<std::string> new_bodies = lines;
    std::map<std::size_t, std::vector<std::string>> inserts;
    auto set_line = [&](std::size_t idx, const std::string& new_body) {
        if (new_body != lines[idx]) {
            res.changes.push_back(RetimeChange{idx + 1, lines[idx], new_body, false});
            new_bodies[idx] = new_body;
        }
    };

    set_line(project_file_lines.front(), "file=" + out_utf8);

    for (const auto& [scene_id, scene_idxs] : scene_lines) {  // ascending by scene ID (the order of the changes)
        const SceneRate& sr = scene_rate_lines.at(scene_id);
        if (!sr.follow) {
            continue;  // a kept scene: its section is written byte for byte
        }
        for (std::size_t idx : scene_idxs) {
            const std::string key = key_of(lines[idx]);
            const std::string value = value_of(lines[idx]);
            if (idx == sr.rate_idx) {
                set_line(idx, "video.rate=" + std::to_string(target_num));
                if (!sr.scale_idx) {
                    const std::string inserted = "video.scale=" + std::to_string(target_den);
                    inserts[idx].push_back(inserted);
                    res.changes.push_back(RetimeChange{idx + 1, std::nullopt, inserted, true});
                }
            } else if (sr.scale_idx && idx == *sr.scale_idx) {
                if (parse_digits(value, idx + 1, "video.scale") != target_den) {
                    set_line(idx, "video.scale=" + std::to_string(target_den));
                }
            } else if (kSceneFrameKeys.count(key) != 0) {
                set_line(idx, key + "=" + std::to_string(round_scaled(parse_digits(value, idx + 1, key), sr.k)));
            }
        }
    }

    std::vector<Rational> start_errors;
    std::vector<Rational> end_errors;
    for (const auto& [obj, idx] : followed_frame_lines) {
        const Rational& obj_rate_src = scene_of_object(obj).rate;
        const auto& old = frame_values[obj];
        const auto& nw = new_frames[obj];
        set_line(idx, "frame=" + [&written = written_frames.at(obj)] {
            std::string s;
            for (std::size_t i = 0; i < written.size(); ++i) {
                if (i > 0) {
                    s += ",";
                }
                s += std::to_string(written[i]);
            }
            return s;
        }());
        const Rational start_err = rat_abs(rat_sub(rat_div(Rational{nw.front(), 1}, rate_dst),
                                                   rat_div(Rational{old.front(), 1}, obj_rate_src)));
        const Rational end_err = rat_abs(rat_sub(rat_div(Rational{checked_add(nw.back(), 1), 1}, rate_dst),
                                                 rat_div(Rational{checked_add(old.back(), 1), 1}, obj_rate_src)));
        start_errors.push_back(start_err);
        end_errors.push_back(end_err);
        RetimeObjectTiming timing;
        timing.line = idx + 1;
        timing.start_error_s = fraction_str(start_err);
        timing.end_error_s = fraction_str(end_err);
        timing.shortened = shortened.count(idx + 1) != 0;
        res.objects.push_back(timing);
    }

    Rational max_start{0, 1};
    for (const auto& e : start_errors) {
        if (rat_less(max_start, e)) {
            max_start = e;
        }
    }
    Rational max_end{0, 1};
    for (const auto& e : end_errors) {
        if (rat_less(max_end, e)) {
            max_end = e;
        }
    }
    res.max_start_error_s = fraction_str(max_start);
    res.max_end_error_s = fraction_str(max_end);

    // The precise rows, the frame-counting rows, then the 時間制御 rows (位置 and コマ落ち), each in line order.
    for (const auto& entry : precise_bodies) {
        set_line(entry.first, entry.second);
    }
    for (const auto& entry : frame_mode_bodies) {
        set_line(entry.first, entry.second);
    }
    for (const auto& entry : time_control_bodies) {
        set_line(entry.first, entry.second);
    }

    res.source_rate = fraction_text(rate_src);
    res.target_rate = fraction_text(rate_dst);
    res.k = fraction_text(k);
    if (mode.factor) {
        res.factor = mode.factor_k;
    }

    // Assemble the output bytes. An inserted line goes after its rate line, with that line's EOL.
    std::string out;
    if (has_bom) {
        out += std::string(kBom);
    }
    for (std::size_t idx = 0; idx < records.size(); ++idx) {
        std::string eol = records[idx].eol;
        const auto ins = inserts.find(idx);
        if (ins != inserts.end() && eol.empty()) {
            eol = "\n";
        }
        out += new_bodies[idx] + eol;
        if (ins != inserts.end()) {
            for (const auto& text : ins->second) {
                out += text + eol;
            }
        }
    }
    return out;
}

// ---------------------------------------------------------------------------
// SHA-256 (BCrypt) and JSON
// ---------------------------------------------------------------------------

std::string sha256_hex(std::string_view data) {
    BCRYPT_ALG_HANDLE alg = nullptr;
    BCRYPT_HASH_HANDLE hash = nullptr;
    unsigned char digest[32] = {};
    if (BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA256_ALGORITHM, nullptr, 0) != 0) {
        return std::string();
    }
    bool ok = BCryptCreateHash(alg, &hash, nullptr, 0, nullptr, 0, 0) == 0;
    std::size_t pos = 0;
    while (ok && pos < data.size()) {
        const std::size_t n = std::min<std::size_t>(data.size() - pos, 0x40000000);
        ok = BCryptHashData(hash, const_cast<PUCHAR>(reinterpret_cast<const UCHAR*>(data.data() + pos)),
                            static_cast<ULONG>(n), 0) == 0;
        pos += n;
    }
    ok = ok && BCryptFinishHash(hash, digest, sizeof(digest), 0) == 0;
    if (hash != nullptr) {
        BCryptDestroyHash(hash);
    }
    BCryptCloseAlgorithmProvider(alg, 0);
    if (!ok) {
        return std::string();
    }
    static const char kHex[] = "0123456789abcdef";
    std::string hex;
    for (unsigned char b : digest) {
        hex.push_back(kHex[b >> 4]);
        hex.push_back(kHex[b & 0x0F]);
    }
    return hex;
}

// Python json.dump(ensure_ascii=False): escapes ", \, and control characters only.
std::string json_str(const std::string& s) {
    std::string out = "\"";
    for (unsigned char c : s) {
        switch (c) {
        case '"':
            out += "\\\"";
            break;
        case '\\':
            out += "\\\\";
            break;
        case '\n':
            out += "\\n";
            break;
        case '\r':
            out += "\\r";
            break;
        case '\t':
            out += "\\t";
            break;
        case '\b':
            out += "\\b";
            break;
        case '\f':
            out += "\\f";
            break;
        default:
            if (c < 0x20) {
                char buf[8];
                std::snprintf(buf, sizeof(buf), "\\u%04x", static_cast<unsigned>(c));
                out += buf;
            } else {
                out.push_back(static_cast<char>(c));
            }
        }
    }
    return out + "\"";
}

}  // namespace

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

bool parse_target_rate(std::string_view text, Rational& out, std::string& error) {
    if (text.find('.') != std::string_view::npos) {
        error = "'" + std::string(text) +
                "' is a decimal rate and is refused; give the exact fraction, for example 60000/1001";
        return false;
    }
    const std::size_t slash = text.find('/');
    const std::string_view num_text = text.substr(0, slash);
    const std::string_view den_text = slash == std::string_view::npos ? std::string_view("1") : text.substr(slash + 1);
    if (!all_digits(num_text) || !all_digits(den_text) || (slash != std::string_view::npos && den_text.empty())) {
        error = "must be a positive integer or a fraction such as 66 or 60000/1001, got '" + std::string(text) + "'";
        return false;
    }
    try {
        const i64 num = parse_digits(num_text, 0, "rate");
        const i64 den = parse_digits(den_text, 0, "rate");
        if (num == 0 || den == 0) {
            error = "must be a positive rate, got '" + std::string(text) + "'";
            return false;
        }
        out = make_rational(num, den);
    } catch (const Refused&) {
        error = "'" + std::string(text) + "' is too large for exact 64-bit arithmetic";
        return false;
    } catch (const Overflow&) {
        error = "'" + std::string(text) + "' is too large for exact 64-bit arithmetic";
        return false;
    }
    return true;
}

std::string refusal_message(const RetimeResult& result) {
    if (result.refusal_line == 0) {
        return "REFUSED: " + result.refusal_reason;
    }
    return "REFUSED: line " + std::to_string(result.refusal_line) + ": " + result.refusal_reason;
}

namespace {

RetimeResult run(std::string_view in_utf8, const Mode& mode, std::wstring out_path) {
    RetimeResult res;
    res.input_sha256 = sha256_hex(in_utf8);
    const std::string out_utf8 = wide_to_utf8(out_path);
    try {
        std::string output = convert(in_utf8, mode, out_utf8, res);
        res.ok = true;
        res.output = std::move(output);
        res.output_sha256 = sha256_hex(res.output);
    } catch (const Refused& refused) {
        RetimeResult failed;
        failed.input_sha256 = res.input_sha256;
        failed.refusal_line = refused.line;
        failed.refusal_reason = refused.reason;
        failed.subscene_rate = res.subscene_rate;  // the scene plan, when it was already worked out (for previews)
        failed.scenes = res.scenes;
        return failed;
    } catch (const Overflow&) {
        RetimeResult failed;
        failed.input_sha256 = res.input_sha256;
        failed.refusal_reason = "arithmetic overflow: rate or frame numbers are too large for exact 64-bit arithmetic";
        failed.subscene_rate = res.subscene_rate;
        failed.scenes = res.scenes;
        return failed;
    }
    return res;
}

}  // namespace

RetimeResult retime(std::string_view in_utf8, Rational target, std::wstring out_path, bool precise,
                    std::size_t precise_extra_decimals, SubsceneRatePolicy subscene_rate) {
    Mode mode;
    mode.target = target;
    mode.precise = precise;
    mode.precise_extra_decimals = precise_extra_decimals;
    mode.subscene_rate = subscene_rate;
    return run(in_utf8, mode, std::move(out_path));
}

RetimeResult retime_factor(std::string_view in_utf8, std::int64_t factor, std::wstring out_path, bool precise,
                           std::size_t precise_extra_decimals, SubsceneRatePolicy subscene_rate) {
    Mode mode;
    mode.factor = true;
    mode.factor_k = factor;
    mode.precise = precise;
    mode.precise_extra_decimals = precise_extra_decimals;
    mode.subscene_rate = subscene_rate;
    return run(in_utf8, mode, std::move(out_path));
}

std::string sidecar_json(const RetimeResult& r, const std::wstring& in_path, const std::wstring& out_path) {
    std::vector<std::string> lines;
    lines.push_back("{");
    lines.push_back("  \"tool\": \"aup2_fps_upconvert\",");
    lines.push_back(std::string("  \"factor\": ") + (r.factor ? std::to_string(*r.factor) : "null") + ",");
    lines.push_back("  \"source_rate\": " + json_str(r.source_rate) + ",");
    lines.push_back("  \"target_rate\": " + json_str(r.target_rate) + ",");
    lines.push_back("  \"k\": " + json_str(r.k) + ",");
    lines.push_back("  \"subscene_rate\": " + json_str(r.subscene_rate) + ",");
    if (r.scenes.empty()) {
        lines.push_back("  \"scenes\": [],");
    } else {
        lines.push_back("  \"scenes\": [");
        for (std::size_t i = 0; i < r.scenes.size(); ++i) {
            const ScenePlan& p = r.scenes[i];
            lines.push_back("    {");
            lines.push_back("      \"scene\": " + std::to_string(p.scene) + ",");
            lines.push_back("      \"name\": " + json_str(p.name) + ",");
            lines.push_back("      \"source_rate\": " + json_str(p.source_rate) + ",");
            lines.push_back("      \"target_rate\": " + json_str(p.target_rate) + ",");
            lines.push_back("      \"k\": " + json_str(p.k) + ",");
            lines.push_back("      \"policy\": " + json_str(p.follow ? "follow" : "keep") + ",");
            lines.push_back(std::string("      \"exported\": ") + (p.exported ? "true" : "false"));
            lines.push_back(i + 1 < r.scenes.size() ? "    }," : "    }");
        }
        lines.push_back("  ],");
    }
    lines.push_back("  \"input\": {");
    lines.push_back("    \"path\": " + json_str(wide_to_utf8(in_path)) + ",");
    lines.push_back("    \"sha256\": " + json_str(r.input_sha256));
    lines.push_back("  },");
    lines.push_back("  \"output\": {");
    lines.push_back("    \"path\": " + json_str(wide_to_utf8(out_path)) + ",");
    lines.push_back("    \"sha256\": " + json_str(r.output_sha256));
    lines.push_back("  },");
    lines.push_back("  \"changed_line_count\": " + std::to_string(r.changes.size()) + ",");
    if (r.changes.empty()) {
        lines.push_back("  \"changes\": [],");
    } else {
        lines.push_back("  \"changes\": [");
        for (std::size_t i = 0; i < r.changes.size(); ++i) {
            const RetimeChange& c = r.changes[i];
            lines.push_back("    {");
            lines.push_back("      \"line\": " + std::to_string(c.line) + ",");
            lines.push_back("      \"old\": " + (c.old_text ? json_str(*c.old_text) : std::string("null")) + ",");
            if (c.inserted) {
                lines.push_back("      \"new\": " + json_str(c.new_text) + ",");
                lines.push_back("      \"inserted\": true");
            } else {
                lines.push_back("      \"new\": " + json_str(c.new_text));
            }
            lines.push_back(i + 1 < r.changes.size() ? "    }," : "    }");
        }
        lines.push_back("  ],");
    }
    lines.push_back("  \"timing\": {");
    lines.push_back("    \"max_start_error_s\": " + json_str(r.max_start_error_s) + ",");
    lines.push_back("    \"max_end_error_s\": " + json_str(r.max_end_error_s) + ",");
    if (r.objects.empty()) {
        lines.push_back("    \"objects\": []");
    } else {
        lines.push_back("    \"objects\": [");
        for (std::size_t i = 0; i < r.objects.size(); ++i) {
            const RetimeObjectTiming& o = r.objects[i];
            lines.push_back("      {");
            lines.push_back("        \"line\": " + std::to_string(o.line) + ",");
            lines.push_back("        \"start_error_s\": " + json_str(o.start_error_s) + ",");
            lines.push_back("        \"end_error_s\": " + json_str(o.end_error_s) + ",");
            lines.push_back(std::string("        \"shortened\": ") + (o.shortened ? "true" : "false"));
            lines.push_back(i + 1 < r.objects.size() ? "      }," : "      }");
        }
        lines.push_back("    ]");
    }
    lines.push_back("  },");
    // Python json.dump order: notes, then "precise" (only in precise mode) as the last key.
    const bool precise_follows = r.precise.has_value();
    if (r.notes.empty()) {
        lines.push_back(precise_follows ? "  \"notes\": []," : "  \"notes\": []");
    } else {
        lines.push_back("  \"notes\": [");
        for (std::size_t i = 0; i < r.notes.size(); ++i) {
            lines.push_back("    " + json_str(r.notes[i]) + (i + 1 < r.notes.size() ? "," : ""));
        }
        lines.push_back(precise_follows ? "  ]," : "  ]");
    }
    if (r.precise) {
        const PreciseReport& p = *r.precise;
        lines.push_back("  \"precise\": {");
        lines.push_back("    \"enabled\": true,");
        if (p.extra_decimals > 0) {
            lines.push_back("    \"extra_decimals\": " + std::to_string(p.extra_decimals) + ",");
        }
        lines.push_back("    \"applied\": " + std::to_string(p.applied) + ",");
        if (p.skipped.empty()) {
            lines.push_back("    \"skipped\": [],");
        } else {
            lines.push_back("    \"skipped\": [");
            for (std::size_t i = 0; i < p.skipped.size(); ++i) {
                lines.push_back("      {");
                lines.push_back("        \"line\": " + std::to_string(p.skipped[i].line) + ",");
                lines.push_back("        \"reason\": " + json_str(p.skipped[i].reason));
                lines.push_back(i + 1 < p.skipped.size() ? "      }," : "      }");
            }
            lines.push_back("    ],");
        }
        if (p.tracks.empty()) {
            lines.push_back("    \"tracks\": []");
        } else {
            lines.push_back("    \"tracks\": [");
            for (std::size_t i = 0; i < p.tracks.size(); ++i) {
                const PreciseTrack& t = p.tracks[i];
                lines.push_back("      {");
                lines.push_back("        \"object_line\": " + std::to_string(t.object_line) + ",");
                lines.push_back("        \"line\": " + std::to_string(t.line) + ",");
                lines.push_back("        \"max_quant_error\": " + json_str(t.max_quant_error) + ",");
                lines.push_back("        \"max_quant_error_pct\": " + json_str(t.max_quant_error_pct) + ",");
                lines.push_back("        \"end_extrapolation\": " + json_str(t.end_extrapolation));
                lines.push_back(i + 1 < p.tracks.size() ? "      }," : "      }");
            }
            lines.push_back("    ]");
        }
        lines.push_back("  }");
    }
    lines.push_back("}");

    std::string out;
    for (const auto& line : lines) {
        out += line;
        out += "\n";
    }
    return out;
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
    std::string out(static_cast<std::size_t>(size), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.data(), length, &out[0], size, nullptr, nullptr);
    return out;
}

bool utf8_to_wide(std::string_view text, std::wstring& out) {
    out.clear();
    if (text.empty()) {
        return true;
    }
    const int length = static_cast<int>(text.size());
    const int size = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), length, nullptr, 0);
    if (size <= 0) {
        return false;
    }
    out.resize(static_cast<std::size_t>(size));
    return MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), length, &out[0], size) == size;
}

}  // namespace free_fps
