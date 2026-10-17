// Unit tests of the pure re-timer (retime.cpp). No AviUtl, no window.
//
// Build and run: experiments\free_fps_export\build_and_run.ps1
// The fixture directory defaults to ..\..\66_to_60_validation relative to this file; pass another as argv[1].

#include "../retime.h"

#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>

namespace {

using free_fps::Rational;
using free_fps::RetimeResult;

int g_failures = 0;

void check(bool condition, const std::string& name, const std::string& detail = std::string()) {
    if (condition) {
        std::printf("PASS %s\n", name.c_str());
    } else {
        std::printf("FAIL %s%s%s\n", name.c_str(), detail.empty() ? "" : " : ", detail.c_str());
        ++g_failures;
    }
}

std::string read_file(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    std::ostringstream text;
    text << in.rdbuf();
    return text.str();
}

std::string fixture_dir;

Rational rate(std::int64_t num, std::int64_t den = 1) {
    Rational r;
    r.num = num;
    r.den = den;
    return r;
}

RetimeResult retime_target(const std::string& text, Rational target) {
    return free_fps::retime(text, target, L"D:\\out\\retimed.aup2");
}

// Minimal project: one scene (rate and scale as given), then the given sections.
std::string project(const std::string& scene_lines, const std::string& sections) {
    return "[project]\nversion=2010400\nfile=D:\\old\\src.aup2\n[scene.0]\nscene=0\nname=test\n"
           "video.width=640\nvideo.height=360\n" +
           scene_lines + "audio.rate=44100\ncursor.frame=10\npreview.frame=20\n" + sections;
}

std::string scene30(const std::string& rate_line = "video.rate=30\nvideo.scale=1\n") {
    return rate_line;
}

// A separate object whose effect is テキスト, so that its value is scanned as text.
std::string text_effect(const std::string& value_line) {
    return "[1]\nlayer=1\n[1.0]\neffect.name=テキスト\n" + value_line;
}

// Two objects on layer 0 that touch at frame 10 (30 FPS input).
std::string touching_objects() {
    return "[0]\nlayer=0\nframe=0,9\n[0.0]\neffect.name=標準描画\n"
           "[1]\nlayer=0\nframe=10,19\n[1.0]\neffect.name=標準描画\n";
}

std::string frame_line(const RetimeResult& r, const std::string& text) {
    // Returns the frame= line text found in the output that starts with text.
    std::istringstream in(r.output);
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        if (line.compare(0, text.size(), text) == 0) {
            return line;
        }
    }
    return std::string();
}

bool contains(const std::string& text, const std::string& needle) {
    return text.find(needle) != std::string::npos;
}

// The fixtures are CRLF in a Windows checkout (core.autocrlf) and LF elsewhere, so line-based needles are matched
// after dropping the CR of each CRLF pair. Only CR immediately before LF is dropped.
std::string strip_eol_cr(const std::string& text) {
    std::string out;
    out.reserve(text.size());
    for (std::size_t i = 0; i < text.size(); ++i) {
        if (text[i] == '\r' && i + 1 < text.size() && text[i + 1] == '\n') {
            continue;
        }
        out.push_back(text[i]);
    }
    return out;
}

std::string to_lf(const std::string& text) {
    return strip_eol_cr(text);
}

std::string to_crlf(const std::string& text) {
    std::string out;
    out.reserve(text.size() + text.size() / 16);
    for (char c : text) {
        if (c == '\n') {
            out.push_back('\r');
        }
        out.push_back(c);
    }
    return out;
}

bool contains_lines(const std::string& text, const std::string& needle) {
    return contains(strip_eol_cr(text), needle);
}

// True when the first LF of text is preceded by CR.
bool is_crlf(const std::string& text) {
    const std::size_t lf = text.find('\n');
    return lf != std::string::npos && lf > 0 && text[lf - 1] == '\r';
}

// True when every LF in text is preceded by CR (crlf) or by no CR (!crlf).
bool uniform_eol(const std::string& text, bool crlf) {
    for (std::size_t i = 0; i < text.size(); ++i) {
        if (text[i] != '\n') {
            continue;
        }
        const bool has_cr = i > 0 && text[i - 1] == '\r';
        if (has_cr != crlf) {
            return false;
        }
    }
    return true;
}

// ---------------------------------------------------------------------------

void test_parse_target() {
    Rational r;
    std::string error;
    check(free_fps::parse_target_rate("66", r, error) && r.num == 66 && r.den == 1, "parse: 66");
    check(free_fps::parse_target_rate("60000/1001", r, error) && r.num == 60000 && r.den == 1001,
          "parse: 60000/1001");
    check(free_fps::parse_target_rate("120000/2002", r, error) && r.num == 60000 && r.den == 1001,
          "parse: 120000/2002 reduced to 60000/1001");
    const char* refused[] = {"59.94", "0", "0/1", "1/0", "-66", "", "66/", "/1", " 66", "abc", "66/1001/2"};
    for (const char* text : refused) {
        check(!free_fps::parse_target_rate(text, r, error) && !error.empty(),
              std::string("parse: refuses '") + text + "'");
    }
    check(!free_fps::parse_target_rate("99999999999999999999", r, error), "parse: refuses values beyond 64 bits");
}

void test_fixture_30_to_66() {
    const std::string text = read_file(fixture_dir + "\\freerenderfps_30fps.aup2");
    check(!text.empty(), "fixture: 30 FPS fixture readable");
    const RetimeResult r = retime_target(text, rate(66));
    check(r.ok, "fixture 30->66: converts", refusal_message(r));
    check(frame_line(r, "frame=") == "frame=0,329", "fixture 30->66: frame=0,329", frame_line(r, "frame="));
    check(contains_lines(r.output, "video.rate=66\n"), "fixture 30->66: video.rate=66");
    check(uniform_eol(r.output, is_crlf(text)), "fixture 30->66: output line endings match input");
    check(r.target_rate == "66/1" && r.source_rate == "30/1" && r.k == "11/5", "fixture 30->66: rates as n/d");
    check(r.changes.size() == 3, "fixture 30->66: three changed lines (file, rate, frame)",
          std::to_string(r.changes.size()));
}

void test_fixture_30_to_ntsc() {
    const std::string text = read_file(fixture_dir + "\\freerenderfps_30fps.aup2");
    const RetimeResult r = retime_target(text, rate(60000, 1001));
    check(r.ok, "fixture 30->60000/1001: converts", refusal_message(r));
    check(frame_line(r, "frame=") == "frame=0,299", "fixture 30->60000/1001: frame=0,299", frame_line(r, "frame="));
    check(contains_lines(r.output, "video.rate=60000\nvideo.scale=1001\n"),
          "fixture 30->60000/1001: video.rate=60000 and video.scale=1001");
    check(uniform_eol(r.output, is_crlf(text)), "fixture 30->60000/1001: output line endings match input");
    check(r.k == "2000/1001", "fixture 30->60000/1001: k = 2000/1001", r.k);
}

void test_fixture_66_and_120() {
    const std::string t66 = read_file(fixture_dir + "\\freerenderfps_66fps.aup2");
    const RetimeResult r66 = retime_target(t66, rate(132));
    check(r66.ok && frame_line(r66, "frame=") == "frame=0,659", "fixture 66->132: frame=0,659",
          refusal_message(r66));
    const std::string t120 = read_file(fixture_dir + "\\freerenderfps_120fps.aup2");
    const RetimeResult r120 = retime_target(t120, rate(60));
    check(r120.ok && frame_line(r120, "frame=") == "frame=0,299", "fixture 120->60: frame=0,299",
          refusal_message(r120));
}

// The key fixture cases on both line-ending styles, built in memory from the same LF text. Output must keep the
// style of its input line for line.
void test_fixture_eol_variants() {
    const std::string base = to_lf(read_file(fixture_dir + "\\freerenderfps_30fps.aup2"));
    check(!base.empty() && !is_crlf(base), "eol variants: LF base readable");
    const std::string variants[] = {base, to_crlf(base)};
    const bool crlf[] = {false, true};
    for (int i = 0; i < 2; ++i) {
        const std::string tag = crlf[i] ? "CRLF" : "LF";
        const RetimeResult r66 = retime_target(variants[i], rate(66));
        check(r66.ok && frame_line(r66, "frame=") == "frame=0,329" && contains_lines(r66.output, "video.rate=66\n") &&
                  uniform_eol(r66.output, crlf[i]),
              "eol " + tag + ": 30->66 keeps frame=0,329, video.rate=66 and " + tag + " line endings", refusal_message(r66));
        const RetimeResult ntsc = retime_target(variants[i], rate(60000, 1001));
        check(ntsc.ok && frame_line(ntsc, "frame=") == "frame=0,299" &&
                  contains_lines(ntsc.output, "video.rate=60000\nvideo.scale=1001\n") &&
                  uniform_eol(ntsc.output, crlf[i]),
              "eol " + tag + ": 30->60000/1001 keeps frame=0,299, rate, scale and " + tag + " line endings",
              refusal_message(ntsc));
        const RetimeResult f2 = free_fps::retime_factor(variants[i], 2, L"D:\\out\\retimed.aup2");
        check(f2.ok && frame_line(f2, "frame=") == "frame=0,299" && contains_lines(f2.output, "video.rate=60\nvideo.scale=1\n") &&
                  uniform_eol(f2.output, crlf[i]),
              "eol " + tag + ": factor 2 keeps frame=0,299, rate 60, scale and " + tag + " line endings",
              refusal_message(f2));
        const RetimeResult precise = free_fps::retime(variants[i], rate(60000, 1001), L"D:\\out\\retimed.aup2", true);
        check(precise.ok && frame_line(precise, "X=") == "X=-200.00,201.74,直線移動,0" &&
                  uniform_eol(precise.output, crlf[i]),
              "eol " + tag + ": precise 30->60000/1001 rewrites X and keeps " + tag + " line endings",
              refusal_message(precise));
    }
}

void test_synthetic_60_to_66() {
    const std::string text = project("video.rate=60\nvideo.scale=1\n",
                                     "[0]\nlayer=0\nframe=0,59\n[1]\nlayer=0\nframe=60,119\n");
    const RetimeResult r = retime_target(text, rate(66));
    check(r.ok, "synthetic 60->66: converts", refusal_message(r));
    check(contains(r.output, "frame=0,65\n"), "synthetic 60->66: 0,59 -> 0,65");
    check(contains(r.output, "frame=66,131\n"), "synthetic 60->66: 60,119 -> 66,131");
}

void test_synthetic_72_to_60() {
    const std::string text = project("video.rate=72\nvideo.scale=1\n", "[0]\nlayer=0\nframe=0,71\n");
    const RetimeResult r = retime_target(text, rate(60));
    check(r.ok && contains(r.output, "frame=0,59\n"), "synthetic 72->60: 0,71 -> 0,59", refusal_message(r));
}

void test_touching_neighbours_survive() {
    struct Case {
        Rational target;
        const char* first;
        const char* second;
    };
    const Case cases[] = {
        {rate(66), "frame=0,21", "frame=22,43"},
        {rate(60000, 1001), "frame=0,19", "frame=20,39"},
        {rate(24), "frame=0,7", "frame=8,15"},
        {rate(59), "frame=0,19", "frame=20,38"},
    };
    for (const Case& c : cases) {
        const std::string text = project(scene30(), touching_objects());
        const RetimeResult r = retime_target(text, c.target);
        const std::string name = "touching at " + std::to_string(c.target.num) + "/" + std::to_string(c.target.den);
        check(r.ok, name + ": converts", refusal_message(r));
        if (!r.ok) {
            continue;
        }
        const bool touch = contains(r.output, std::string(c.first) + "\n") &&
                           contains(r.output, std::string(c.second) + "\n");
        check(touch, name + ": neighbours still touch (" + c.first + ", " + c.second + ")");
    }
}

void test_shortened_object() {
    const std::string text = project(scene30(), "[0]\nlayer=0\nframe=2,2\n");
    const RetimeResult r = retime_target(text, rate(24));
    check(r.ok, "shortened: 30->24 converts", refusal_message(r));
    check(frame_line(r, "frame=") == "frame=2,2", "shortened: frame=2,2 stays one frame", frame_line(r, "frame="));
    check(r.notes.size() == 1 && r.notes[0] == "object at line 16 shortened to 1 frame", "shortened: note text",
          r.notes.empty() ? "" : r.notes[0]);
    check(r.objects.size() == 1 && r.objects[0].shortened, "shortened: timing marks the object");
}

void test_video_scale_missing_inserted() {
    const std::string text = project("video.rate=30\n", "[0]\nlayer=0\nframe=0,29\n");
    const RetimeResult r = retime_target(text, rate(60));
    check(r.ok, "scale missing: converts", refusal_message(r));
    check(contains(r.output, "video.rate=60\nvideo.scale=1\n"), "scale missing: video.scale=1 inserted after rate");
    bool inserted_null_old = false;
    for (const auto& c : r.changes) {
        inserted_null_old = inserted_null_old || (c.inserted && !c.old_text && c.new_text == "video.scale=1");
    }
    check(inserted_null_old, "scale missing: inserted change has null old");
    check(!r.notes.empty() && contains(r.notes[0], "has no video.scale; read as 1"), "scale missing: note");
}

void test_bom_and_crlf_preserved() {
    std::string text = project("video.rate=30\r\nvideo.scale=1\r\n", "[0]\r\nlayer=0\r\nframe=0,29\r\n");
    const std::string with_bom = std::string("\xEF\xBB\xBF") + text;
    const RetimeResult r = retime_target(with_bom, rate(60));
    check(r.ok, "bom/crlf: converts", refusal_message(r));
    check(r.output.compare(0, 3, "\xEF\xBB\xBF") == 0, "bom/crlf: BOM kept");
    check(contains(r.output, "video.rate=60\r\n") && contains(r.output, "frame=0,59\r\n"),
          "bom/crlf: CRLF kept on rewritten lines");
}

void test_factor_mode() {
    const std::string text = read_file(fixture_dir + "\\freerenderfps_30fps.aup2");
    const RetimeResult r = free_fps::retime_factor(text, 2, L"D:\\out\\retimed.aup2");
    check(r.ok && contains_lines(r.output, "video.rate=60\nvideo.scale=1\n") && frame_line(r, "frame=") == "frame=0,299",
          "factor 2: rate 60, scale kept, frame=0,299", refusal_message(r));
    check(uniform_eol(r.output, is_crlf(text)), "factor 2: output line endings match input");
    check(r.factor && *r.factor == 2 && r.k == "2/1", "factor 2: factor and k = 2/1");
    const std::string json = free_fps::sidecar_json(r, L"D:\\in.aup2", L"D:\\out\\retimed.aup2");
    check(contains(json, "\"factor\": 2,"), "factor 2: sidecar has factor 2");
    const RetimeResult refused = free_fps::retime_factor(text, 1, L"D:\\out\\retimed.aup2");
    check(!refused.ok && refusal_message(refused) == "REFUSED: factor must be an integer >= 2, got 1",
          "factor 1: refused", refusal_message(refused));
}

void test_sidecar_shape() {
    const std::string text = read_file(fixture_dir + "\\freerenderfps_30fps.aup2");
    const RetimeResult r = retime_target(text, rate(66));
    const std::string json = free_fps::sidecar_json(r, L"D:\\in.aup2", L"D:\\out\\retimed.aup2");
    check(contains(json, "\"factor\": null,"), "sidecar: factor null in target mode");
    check(contains(json, "\"source_rate\": \"30/1\","), "sidecar: source_rate n/d");
    check(contains(json, "\"target_rate\": \"66/1\","), "sidecar: target_rate n/d");
    check(contains(json, "\"k\": \"11/5\","), "sidecar: k n/d");
    check(contains(json, "\"path\": \"D:\\\\out\\\\retimed.aup2\""), "sidecar: output path escaped");
    check(contains(json, "\"inserted\": true") == false, "sidecar: no inserted line for a 30 FPS fixture with scale");
    check(contains(json, "\"objects\": ["), "sidecar: timing objects listed");
    check(contains(json, "\"notes\": []"), "sidecar: empty notes list");
}

// ---------------------------------------------------------------------------
// Refusals
// ---------------------------------------------------------------------------

void expect_refused(const std::string& name, const std::string& text, Rational target, std::size_t line,
                    const std::string& reason_part) {
    const RetimeResult r = retime_target(text, target);
    const bool ok_refusal = !r.ok && r.refusal_line == line && contains(r.refusal_reason, reason_part);
    check(ok_refusal, name,
          r.ok ? std::string("converted instead of refusing")
               : "line " + std::to_string(r.refusal_line) + ": " + r.refusal_reason);
}

void test_refusals() {
    const std::string base = project(scene30(), "[0]\nlayer=0\nframe=0,29\n[0.0]\neffect.name=標準描画\n");
    // Line numbers: base has 14 lines before "[0]" ... compute from the text itself.
    auto line_of = [](const std::string& text, const std::string& needle) {
        std::size_t line = 1;
        for (std::size_t i = 0; i < text.size(); ++i) {
            if (text[i] == '\n') {
                ++line;
            }
            if (text.compare(i, needle.size(), needle) == 0) {
                return line;
            }
        }
        return std::size_t(0);
    };

    {
        const std::string text = base + "コマ落ち=1\n";
        expect_refused("refuses コマ落ち=1", text, rate(60), line_of(text, "コマ落ち=1"), "コマ落ち=1");
    }
    {
        const std::string text = base + "X=0,100,直線移動,0|27\n";
        expect_refused("refuses trackbar | payload", text, rate(60), line_of(text, "X=0,100"), "'|' payload");
    }
    {
        const std::string text = base + "X=0,100,謎移動,0\n";
        expect_refused("refuses unknown trackbar mode", text, rate(60), line_of(text, "X=0,100"), "謎移動");
    }
    {
        const std::string text = base + "X=0,100,直線移動,0\n";
        check(retime_target(text, rate(60)).ok, "accepts whitelisted trackbar 直線移動");
    }
    {
        const std::string text = project(scene30() + "keyframe_hint=1\n", "[0]\nlayer=0\nframe=0,29\n");
        expect_refused("refuses unknown scene key with frame/mark/range", text, rate(60),
                       line_of(text, "keyframe_hint"), "keyframe_hint");
    }
    {
        const std::string text = base + "Y=obj.frame*2\n";
        expect_refused("refuses obj.frame in script", text, rate(60), line_of(text, "Y=obj.frame"), "obj.frame");
    }
    {
        const std::string text = base + "Y=rand(1,2)\n";
        expect_refused("refuses rand(", text, rate(60), line_of(text, "Y=rand"), "rand");
    }
    {
        const std::string text = base + "Y=math.random(1)\n";
        expect_refused("refuses math.random(", text, rate(60), line_of(text, "Y=math.random"), "math.random");
    }
    {
        const std::string text = base + "Y=math.randomseed(1)\n";
        expect_refused("refuses math.randomseed", text, rate(60), line_of(text, "Y=math.randomseed"),
                       "math.randomseed");
    }
    {
        const std::string text = base + "Y=obj.time*2\n";
        check(retime_target(text, rate(60)).ok, "accepts time-based obj.time");
    }
    {
        const std::string text = base + text_effect("テキスト=<?obj.frame?>\n");
        expect_refused("refuses obj.frame inside テキスト <? ?>", text, rate(60),
                       line_of(text, "テキスト=<?obj"), "obj.frame");
    }
    {
        const std::string text = base + text_effect("テキスト=random rand obj.x\n");
        check(retime_target(text, rate(60)).ok, "accepts plain text outside <? ?> with rand/obj.x");
    }
    {
        const std::string text = base + text_effect("テキスト=<?\\nobj.frame?>\n");
        expect_refused("refuses escaped newline then obj.frame in テキスト", text, rate(60),
                       line_of(text, "テキスト=<?"), "obj.frame");
    }
    {
        const std::string text = base + "テキスト=あobj.frame\n";
        check(retime_target(text, rate(60)).ok, "accepts obj.frame glued to a Japanese letter (no \\b)");
    }
    {
        const std::string text = base + "カウンター=1\neffect.name=カウンター\n速度=abc\n";
        check(!retime_target(text, rate(60)).ok, "refuses カウンター with non-numeric 速度");
    }
    {
        const std::string text = base + "effect.name=カウンター\n速度=1.5\n";
        const RetimeResult r = retime_target(text, rate(60));
        check(r.ok && r.notes.size() == 1 && contains(r.notes[0], "カウンター: time-based"),
              "accepts カウンター with numeric 速度, with note", refusal_message(r));
    }
    {
        const std::string text = project(scene30(), "[0]\nlayer=0\nframe=5,3\n");
        expect_refused("refuses end before start", text, rate(60), line_of(text, "frame=5,3"), "is before start");
    }
    {
        const std::string text = project(scene30(), "[0]\nlayer=0\nframe=0,20,10,29\n");
        expect_refused("refuses non-increasing intermediate points", text, rate(60), line_of(text, "frame=0,20"),
                       "strictly increasing");
    }
    {
        // At 14/1 (k = 0.4667) the first two points both round to 0.
        const std::string text = project(scene30(), "[0]\nlayer=0\nframe=0,1,2,5\n");
        expect_refused("refuses points that collide after rescaling", text, rate(14),
                       line_of(text, "frame=0,1,2,5"), "collide");
    }
    {
        const std::string text = project(scene30(), "[0]\nframe=0,9\n[0.0]\neffect.name=X\nX=0,1,2,直線移動,0\n");
        expect_refused("refuses trackbar value count != frame points", text, rate(60), line_of(text, "X=0,1,2"),
                       "trackbar has 3 values");
    }
    {
        const std::string text = project(scene30(), "[0]\nframe=0,9\n[0]\nframe=1,2\n");
        expect_refused("refuses duplicate frame=", text, rate(60), line_of(text, "frame=1,2"),
                       "duplicate frame=");
    }
    {
        const std::string text = project("video.rate=30\nvideo.scale=1\n[scene.1]\nscene=1\nvideo.rate=60\n"
                                         "video.scale=1\n",
                                         "");
        expect_refused("refuses scenes with different rates", text, rate(66), 0, "do not share one");
    }
    {
        const std::string text = project(scene30(), "[0]\nlayer=0\nframe=0,0\n[1]\nlayer=0\nframe=1,5\n");
        expect_refused("refuses touching pair that stops touching (1-frame object)", text, rate(14),
                       line_of(text, "frame=1,5"), "no longer touch");
    }
    {
        // Same geometry, but the second object is on another scene: no same-layer rule applies.
        const std::string text = project(scene30(), "[0]\nlayer=0\nframe=0,0\n[1]\nlayer=0\nscene=1\nframe=1,5\n");
        check(retime_target(text, rate(14)).ok, "accepts the same geometry when the objects are on different scenes");
    }
    {
        // Gap between frame 3 and frame 5 in the input. At 6/1 (k = 0.2) the 1-frame object rounds to [1,1] and
        // the next object starts at 1 as well, so they overlap after rescaling.
        const std::string text = project(scene30(), "[0]\nlayer=0\nframe=3,3\n[1]\nlayer=0\nframe=5,9\n");
        expect_refused("refuses overlap on same scene/layer after rescaling", text, rate(6),
                       line_of(text, "frame=5,9"), "overlaps the object");
    }
    {
        const std::string bad = project(scene30(), "[0]\nlayer=0\nframe=0,9\n") + std::string("\xFF\xFE", 2);
        const RetimeResult r = retime_target(bad, rate(60));
        check(!r.ok && contains(r.refusal_reason, "not valid UTF-8"), "refuses invalid UTF-8");
    }
    {
        const std::string text = project(scene30(), "[0]\nlayer=0\nframe=0,9\n[0]\nscene=x\n");
        expect_refused("refuses non-integer object scene=", text, rate(60), line_of(text, "scene=x"),
                       "object scene= must be an integer");
    }
    {
        const std::string text = project(scene30(), "[0]\nlayer=0\nframe=0,9\n");
        const RetimeResult r = retime_target(text, rate(60));
        check(r.ok, "accepts a plain 30 FPS object as a baseline", refusal_message(r));
    }
}

// ---------------------------------------------------------------------------
// Precise mode. Trackbar rows are compared with frame_line(), which drops the CR of CRLF fixtures.
// ---------------------------------------------------------------------------

// 1-based line of the first occurrence of needle in text (0 when absent).
std::size_t line_of_first(const std::string& text, const std::string& needle) {
    std::size_t line = 1;
    for (std::size_t i = 0; i < text.size(); ++i) {
        if (text.compare(i, needle.size(), needle) == 0) {
            return line;
        }
        if (text[i] == '\n') {
            ++line;
        }
    }
    return 0;
}

void test_precise_fixtures() {
    const std::string t30 = read_file(fixture_dir + "\\freerenderfps_30fps.aup2");
    const RetimeResult ntsc = free_fps::retime(t30, rate(60000, 1001), L"D:\\out\\retimed.aup2", true);
    check(ntsc.ok, "precise 30->60000/1001: converts", refusal_message(ntsc));
    check(frame_line(ntsc, "frame=") == "frame=0,299", "precise 30->60000/1001: frame=0,299", frame_line(ntsc, "frame="));
    check(frame_line(ntsc, "X=") == "X=-200.00,201.74,直線移動,0", "precise 30->60000/1001: X ends at 201.74",
          frame_line(ntsc, "X="));
    check(frame_line(ntsc, "Y=") == "Y=0.00,0.00,直線移動,0", "precise 30->60000/1001: constant Y unchanged",
          frame_line(ntsc, "Y="));
    check(ntsc.precise && ntsc.precise->applied == 1 && ntsc.precise->skipped.empty() &&
              ntsc.precise->tracks.size() == 3,
          "precise 30->60000/1001: one object applied, three trackbar rows reported");
    check(ntsc.notes.empty(), "precise 30->60000/1001: no quantization warning (0.0009% of the change)");
    if (ntsc.precise && !ntsc.precise->tracks.empty()) {
        const free_fps::PreciseTrack& x = ntsc.precise->tracks[0];
        check(x.object_line == 36 && x.line == 47, "precise: X report names object line 36 and row line 47",
              std::to_string(x.object_line) + "/" + std::to_string(x.line));
        check(x.max_quant_error == "0.003624", "precise: X max quantization error, 6 decimals", x.max_quant_error);
        check(x.max_quant_error_pct == "0.000906", "precise: X error as % of the range, 6 decimals",
              x.max_quant_error_pct);
        check(x.end_extrapolation == "1.740000", "precise: X end extrapolation, 6 decimals", x.end_extrapolation);
    }
    const std::string ntsc_json = free_fps::sidecar_json(ntsc, L"D:\\in.aup2", L"D:\\out\\retimed.aup2");
    check(contains(ntsc_json, "  \"notes\": [],\n  \"precise\": {\n    \"enabled\": true,\n    \"applied\": 1,\n"
                              "    \"skipped\": [],\n    \"tracks\": [\n      {\n        \"object_line\": 36,\n"),
          "precise: sidecar has the precise key after notes, with enabled, applied, skipped, tracks");
    check(!contains(free_fps::sidecar_json(retime_target(t30, rate(60000, 1001)), L"D:\\in.aup2",
                                           L"D:\\out\\retimed.aup2"),
                    "\"precise\""),
          "precise off: no precise key in the sidecar");

    const RetimeResult extra = free_fps::retime(t30, rate(60000, 1001), L"D:\\out\\retimed.aup2", true, 2);
    check(extra.ok && frame_line(extra, "X=") == "X=-200.0000,201.7436,直線移動,0",
          "precise extra decimals 2: X has 4 decimals", frame_line(extra, "X="));

    const RetimeResult f2 = free_fps::retime_factor(t30, 2, L"D:\\out\\retimed.aup2", true);
    check(f2.ok && frame_line(f2, "X=") == "X=-200.00,201.34,直線移動,0", "precise factor 2: X ends at 201.34",
          refusal_message(f2));

    const std::string t66 = read_file(fixture_dir + "\\freerenderfps_66fps.aup2");
    const RetimeResult p24 = free_fps::retime(t66, rate(24), L"D:\\out\\retimed.aup2", true);
    check(p24.ok && frame_line(p24, "frame=") == "frame=0,119", "precise 66->24: frame=0,119", refusal_message(p24));
    check(frame_line(p24, "X=") == "X=-200.00,197.87,直線移動,0", "precise 66->24: X ends at 197.87",
          frame_line(p24, "X="));
}

void test_precise_off_is_unchanged() {
    const std::string text = read_file(fixture_dir + "\\freerenderfps_30fps.aup2");
    const RetimeResult plain = retime_target(text, rate(60000, 1001));
    const RetimeResult off = free_fps::retime(text, rate(60000, 1001), L"D:\\out\\retimed.aup2", false);
    check(plain.ok && off.ok && plain.output == off.output, "precise off: output identical to the default call");
    check(frame_line(plain, "X=") == "X=-200.00,200.00,直線移動,0", "precise off: trackbar values unchanged",
          frame_line(plain, "X="));
    check(!plain.precise && !off.precise, "precise off: no precise report");
    check(!contains(free_fps::sidecar_json(plain, L"D:\\in.aup2", L"D:\\out\\retimed.aup2"), "\"precise\""),
          "precise off: sidecar has no precise key");
}

void test_precise_skips_ineligible_objects() {
    const std::string body = "[0]\nlayer=0\nframe=0,29\n[0.0]\neffect.name=標準描画\nX=0.00,100.00,補間移動,0\n"
                             "[1]\nlayer=1\nframe=0,29\n[1.0]\neffect.name=標準描画\nZ=0.00,50.00,直線移動,0\n"
                             "[2]\nlayer=2\nframe=0,29\n[2.0]\neffect.name=標準描画\nW=0.00,100.00,直線移動,0\n"
                             "R=0,29,再生範囲,1\n";
    const std::string text = project(scene30(), body);
    const RetimeResult r = retime_target(text, rate(60));
    check(r.ok, "precise skip: converts without precise mode", refusal_message(r));
    const RetimeResult p = free_fps::retime(text, rate(60), L"D:\\out\\retimed.aup2", true);
    check(p.ok, "precise skip: converts in precise mode", refusal_message(p));
    check(p.precise && p.precise->applied == 2, "precise skip: objects 1 and 2 applied");
    check(p.precise && p.precise->skipped.size() == 1 &&
              p.precise->skipped[0].line == line_of_first(text, "frame=0,29\n[0.0]") &&
              contains(p.precise->skipped[0].reason, "補間移動"),
          "precise skip: object 0 skipped at its frame= line, reason names the mode",
          p.precise && !p.precise->skipped.empty() ? p.precise->skipped[0].reason : std::string("no skip"));
    check(frame_line(p, "X=") == "X=0.00,100.00,補間移動,0", "precise skip: skipped row keeps its values",
          frame_line(p, "X="));
    check(frame_line(p, "Z=") == "Z=0.00,50.86,直線移動,0", "precise: eligible object 1 is re-sampled",
          frame_line(p, "Z="));
    check(frame_line(p, "W=") == "W=0.00,101.72,直線移動,0", "precise: eligible object 2 is re-sampled",
          frame_line(p, "W="));
    check(frame_line(p, "R=") == "R=0,29,再生範囲,1", "precise: 再生範囲 row is left alone", frame_line(p, "R="));
}

void test_precise_zero_length_skipped() {
    // A 2-point object with start == end has no segment: the Python tool divides by zero there, the port skips it.
    const std::string text = project(scene30(), "[0]\nlayer=0\nframe=5,5\n[0.0]\neffect.name=標準描画\n"
                                                 "X=0.00,10.00,直線移動,0\n"
                                                 "[1]\nlayer=1\nframe=5,5\n[1.0]\neffect.name=標準描画\n"
                                                 "Y=3.00,3.00,直線移動,0\n");
    const RetimeResult p = free_fps::retime(text, rate(60), L"D:\\out\\retimed.aup2", true);
    check(p.ok && p.precise && p.precise->applied == 0 && p.precise->skipped.size() == 2 &&
              p.precise->skipped[0].line == line_of_first(text, "frame=5,5\n[0.0]") &&
              contains(p.precise->skipped[0].reason, "start equals end") &&
              contains(p.precise->skipped[1].reason, "start equals end"),
          "precise: a 2-point object with start == end is skipped, linear or constant", refusal_message(p));
    check(frame_line(p, "Y=") == "Y=3.00,3.00,直線移動,0", "precise: the skipped constant row keeps its values");
}

void test_precise_rounding_warning() {
    // 30 -> 15: the object becomes frames 0..4. At frame 4 the value is -1/900, which rounds to 0.00 (no "-0.00").
    const std::string text = project(scene30(), "[0]\nlayer=0\nframe=0,9\n[0.0]\neffect.name=標準描画\n"
                                                "X=-0.01,0.00,直線移動,0\n");
    const RetimeResult p = free_fps::retime(text, rate(15), L"D:\\out\\retimed.aup2", true);
    check(p.ok, "precise rounding: converts", refusal_message(p));
    check(frame_line(p, "X=") == "X=-0.01,0.00,直線移動,0", "precise rounding: -1/900 rounds to 0.00, not -0.00",
          frame_line(p, "X="));
    check(p.precise && p.precise->tracks.size() == 1 && p.precise->tracks[0].max_quant_error == "0.001111" &&
              p.precise->tracks[0].max_quant_error_pct == "11.111111" &&
              p.precise->tracks[0].end_extrapolation == "0.000000",
          "precise rounding: error 0.001111, 11.111111% of the range, end extrapolation 0.000000");
    const std::string x_line = std::to_string(line_of_first(text, "X=-0.01"));
    const std::string frame_line_no = std::to_string(line_of_first(text, "frame=0,9"));
    check(p.notes.size() == 1 &&
              contains(p.notes[0], "precise: trackbar at line " + x_line + " (object at line " + frame_line_no + ")") &&
              contains(p.notes[0], "quantization error is 11.111111% of its range, above 0.01%"),
          "precise rounding: warning in notes above 0.01%", p.notes.empty() ? "" : p.notes[0]);
}

void test_precise_overflow_refused() {
    // 19 decimals: 10^19 does not fit in 64 bits.
    const std::string text = project(scene30(), "[0]\nlayer=0\nframe=0,29\n[0.0]\neffect.name=標準描画\n"
                                                "X=0.0000000000000000001,1.00,直線移動,0\n");
    const RetimeResult p = free_fps::retime(text, rate(60), L"D:\\out\\retimed.aup2", true);
    check(!p.ok && contains(p.refusal_reason, "overflow"), "precise: overflow is refused", refusal_message(p));
}

}  // namespace

int main(int argc, char** argv) {
    if (argc > 1) {
        fixture_dir = argv[1];
    } else {
        std::string here = __FILE__;
        const std::size_t cut = here.find_last_of("\\/");
        here = here.substr(0, cut == std::string::npos ? 0 : cut);  // .../free_fps_export/tests
        fixture_dir = here + "\\..\\..\\66_to_60_validation";
    }
    test_parse_target();
    test_fixture_30_to_66();
    test_fixture_30_to_ntsc();
    test_fixture_66_and_120();
    test_fixture_eol_variants();
    test_synthetic_60_to_66();
    test_synthetic_72_to_60();
    test_touching_neighbours_survive();
    test_shortened_object();
    test_video_scale_missing_inserted();
    test_bom_and_crlf_preserved();
    test_factor_mode();
    test_sidecar_shape();
    test_refusals();
    test_precise_fixtures();
    test_precise_off_is_unchanged();
    test_precise_skips_ineligible_objects();
    test_precise_zero_length_skipped();
    test_precise_rounding_warning();
    test_precise_overflow_refused();
    std::printf("%s (%d failure%s)\n", g_failures == 0 ? "ALL PASS" : "FAILED", g_failures,
                g_failures == 1 ? "" : "s");
    return g_failures == 0 ? 0 : 1;
}
