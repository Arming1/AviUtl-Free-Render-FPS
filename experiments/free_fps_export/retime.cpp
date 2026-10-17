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
const std::set<std::string> kTrackModes = {"直線移動", "補間移動", "瞬間移動", "移動無し", "回転", "再生範囲"};
const std::set<std::string> kTrackCountExempt = {"再生範囲"};
const std::string kCounterEffect = "カウンター";
const std::string kSpeedKey = "速度";
const std::string kColumnKey = "コマ落ち";

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

struct SceneRate {
    std::size_t rate_idx = 0;
    std::optional<std::size_t> scale_idx;
};

struct Mode {
    bool factor = false;
    i64 factor_k = 0;
    Rational target;
};

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
    bool project_seen = false;
    std::map<i64, std::vector<std::size_t>> scene_lines;
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
                if (scene_lines.find(n) == scene_lines.end()) {
                    scene_lines[n];
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
            }
            break;
        case Kind::kScene:
            scene_lines[section.a].push_back(idx);
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
        if (key == kColumnKey && value == "1") {
            refuse(idx + 1, kColumnKey + "=1 is not supported");
        }
    }

    if (!project_seen || project_file_lines.size() != 1) {
        refuse(0, "expected exactly one 'file=' line in [project]");
    }
    if (scene_lines.empty()) {
        refuse(0, "no [scene.N] section found");
    }

    // Each object belongs to one scene: its scene= line if it has one, else the [scene.N] section above it,
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
            object_scene[obj] = obj_header_scene[obj].value_or(0);
        }
    }

    // Pass 2: scene checks. All scenes must share one rate/scale.
    std::set<std::pair<i64, i64>> rates;
    std::map<i64, SceneRate> scene_rate_lines;
    for (const auto& [scene_no, idxs] : scene_lines) {
        std::optional<std::pair<i64, std::size_t>> rate_v;
        std::optional<std::pair<i64, std::size_t>> scale_v;
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
            } else {
                for (const auto& marker : kSceneRefuseMarkers) {
                    if (key.find(marker) != std::string::npos) {
                        refuse(idx + 1, "unknown scene key '" + key + "' may hold frame/mark/range data");
                    }
                }
            }
        }
        if (!rate_v) {
            refuse(0, "[scene." + std::to_string(scene_no) + "] has no video.rate");
        }
        i64 scale_value = 1;
        std::optional<std::size_t> scale_idx;
        if (scale_v) {
            scale_value = scale_v->first;
            scale_idx = scale_v->second;
        } else {
            res.notes.push_back("[scene." + std::to_string(scene_no) +
                                "] has no video.scale; read as 1 and written after video.rate");
        }
        scene_rate_lines[scene_no] = SceneRate{rate_v->second, scale_idx};
        rates.insert(std::make_pair(rate_v->first, scale_value));
    }
    if (rates.size() != 1) {
        std::string list = "[";
        for (auto it = rates.begin(); it != rates.end(); ++it) {
            if (it != rates.begin()) {
                list += ", ";
            }
            list += "(" + std::to_string(it->first) + ", " + std::to_string(it->second) + ")";
        }
        refuse(0, "scenes do not share one video.rate/video.scale: " + list + "]");
    }
    const i64 src_rate = rates.begin()->first;
    const i64 src_scale = rates.begin()->second;

    // Target rate: factor mode keeps video.scale (not reduced); target mode writes the reduced fraction.
    i64 target_num = mode.target.num;
    i64 target_den = mode.target.den;
    if (mode.factor) {
        target_num = checked_mul(src_rate, mode.factor_k);
        target_den = src_scale;
    }
    const Rational rate_src = make_rational(src_rate, src_scale);
    const Rational rate_dst = make_rational(target_num, target_den);
    const Rational k = rat_div(rate_dst, rate_src);

    // Pass 3: object frame lists, and trackbars that depend on them.
    std::map<i64, std::vector<i64>> frame_values;
    for (const auto& [obj, idx] : frame_lines) {
        frame_values[obj] = parse_frames(value_of(lines[idx]), idx + 1);
    }
    std::vector<std::size_t> counter_lines;
    for (const auto& entry : effect_props) {
        const auto& props = entry.second;
        const auto name = props.find("effect.name");
        if (name == props.end() || name->second.value != kCounterEffect) {
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

    for (std::size_t idx : effect_lines) {
        const Section sec = section_of[idx];
        const std::string key = key_of(lines[idx]);
        const std::string value = value_of(lines[idx]);
        const auto& props = effect_props.at(std::make_pair(sec.a, sec.b));
        std::optional<std::string> effect_name;
        const auto name = props.find("effect.name");
        if (name != props.end()) {
            effect_name = name->second.value;
        }
        std::string marker;
        if (find_marker(decode_utf8(script_text(effect_name, key, value)), marker)) {
            refuse(idx + 1, "script text uses frame-dependent marker '" + marker + "' in " +
                                effect_name.value_or(std::string("None")) + " effect (" + key +
                                "); refused");
        }
        if (!is_trackbar(value)) {
            continue;
        }
        const auto parts = split_on(value, ',');
        const std::string& mode_name = parts[parts.size() - 2];
        const std::string& param = parts.back();
        if (kTrackModes.count(mode_name) == 0) {
            refuse(idx + 1, "trackbar mode '" + mode_name + "' is not on the whitelist");
        }
        const std::size_t bar = param.find('|');
        if (bar != std::string::npos) {
            refuse(idx + 1, "trackbar '|' payload '" + param.substr(bar + 1) + "' is not on the whitelist");
        }
        const i64 obj = sec.a;
        const auto fv = frame_values.find(obj);
        if (fv != frame_values.end() && kTrackCountExempt.count(mode_name) == 0) {
            const std::size_t count = parts.size() - 2;
            if (count != fv->second.size()) {
                refuse(idx + 1, "trackbar has " + std::to_string(count) + " values but object [" +
                                    std::to_string(obj) + "] has " + std::to_string(fv->second.size()) +
                                    " frame points");
            }
        }
    }

    // Rescale every object, then check the layers.
    std::map<i64, std::vector<i64>> new_frames;
    std::set<std::size_t> shortened;
    for (const auto& [obj, idx] : frame_lines) {
        bool was_short = false;
        new_frames[obj] = rescale_frames(frame_values[obj], k, idx + 1, was_short);
        if (was_short) {
            shortened.insert(idx + 1);
            res.notes.push_back("object at line " + std::to_string(idx + 1) + " shortened to 1 frame");
        }
    }
    check_layers(frame_lines, object_layer, object_scene, frame_values, new_frames);

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

    for (i64 scene_no : scene_order) {
        const SceneRate& sr = scene_rate_lines[scene_no];
        for (std::size_t idx : scene_lines[scene_no]) {
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
                set_line(idx, key + "=" + std::to_string(round_scaled(parse_digits(value, idx + 1, key), k)));
            }
        }
    }

    std::vector<Rational> start_errors;
    std::vector<Rational> end_errors;
    for (const auto& [obj, idx] : frame_lines) {
        const auto& old = frame_values[obj];
        const auto& nw = new_frames[obj];
        set_line(idx, "frame=" + [&nw] {
            std::string s;
            for (std::size_t i = 0; i < nw.size(); ++i) {
                if (i > 0) {
                    s += ",";
                }
                s += std::to_string(nw[i]);
            }
            return s;
        }());
        const Rational start_err = rat_abs(rat_sub(rat_div(Rational{nw.front(), 1}, rate_dst),
                                                   rat_div(Rational{old.front(), 1}, rate_src)));
        const Rational end_err = rat_abs(rat_sub(rat_div(Rational{checked_add(nw.back(), 1), 1}, rate_dst),
                                                 rat_div(Rational{checked_add(old.back(), 1), 1}, rate_src)));
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
        return failed;
    } catch (const Overflow&) {
        RetimeResult failed;
        failed.input_sha256 = res.input_sha256;
        failed.refusal_reason = "arithmetic overflow: rate or frame numbers are too large for exact 64-bit arithmetic";
        return failed;
    }
    return res;
}

}  // namespace

RetimeResult retime(std::string_view in_utf8, Rational target, std::wstring out_path) {
    Mode mode;
    mode.target = target;
    return run(in_utf8, mode, std::move(out_path));
}

RetimeResult retime_factor(std::string_view in_utf8, std::int64_t factor, std::wstring out_path) {
    Mode mode;
    mode.factor = true;
    mode.factor_k = factor;
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
    if (r.notes.empty()) {
        lines.push_back("  \"notes\": []");
    } else {
        lines.push_back("  \"notes\": [");
        for (std::size_t i = 0; i < r.notes.size(); ++i) {
            lines.push_back("    " + json_str(r.notes[i]) + (i + 1 < r.notes.size() ? "," : ""));
        }
        lines.push_back("  ]");
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
