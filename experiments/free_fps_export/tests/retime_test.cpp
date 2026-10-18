// Unit tests of the pure re-timer (retime.cpp). No AviUtl, no window.
//
// Build and run: experiments\free_fps_export\build_and_run.ps1
// The fixture directory defaults to ..\..\66_to_60_validation relative to this file; pass another as argv[1].

#include "../retime.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

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
        check(precise.ok && frame_line(precise, "X=") == "X=-200.00,199.06,200.00,200.00,直線移動,0" &&
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
        const std::string text = base + "コマ落ち=2\n";
        expect_refused("refuses コマ落ち=2 outside a time control effect", text, rate(60), line_of(text, "コマ落ち=2"),
                       "コマ落ち=2 (frame drop) is only converted inside a 時間制御(オブジェクト) effect");
    }
    {
        const std::string text = base + "コマ落ち=0\n";
        expect_refused("refuses コマ落ち=0", text, rate(60), line_of(text, "コマ落ち=0"),
                       "コマ落ち=0 (frame drop) must be a positive integer");
    }
    {
        const std::string text = base + "コマ落ち=0,10,直線移動,0\n";
        expect_refused("refuses コマ落ち trackbar form", text, rate(60), line_of(text, "コマ落ち=0,10"),
                       "コマ落ち=0,10,直線移動,0 (frame drop) must be a positive integer");
    }
    {
        const std::string text = base + "コマ落ち=1\n";
        const RetimeResult r = retime_target(text, rate(60));
        check(r.ok && contains(r.output, "\nコマ落ち=1\n"), "accepts コマ落ち=1 and keeps the line unchanged",
              r.ok ? std::string() : refusal_message(r));
    }
    {
        const std::string text = base + "X=0,100,補間移動,0|27\n";
        expect_refused("refuses trackbar | payload on another mode", text, rate(60), line_of(text, "X=0,100"),
                       "'|' payload");
    }
    {
        const std::string text = base + "X=0,100,直線移動,0|27\n";
        const RetimeResult r = retime_target(text, rate(60));
        check(r.ok && contains(r.output, "\nX=0,100,直線移動,0|27\n"), "accepts a | payload on 直線移動 and keeps it",
              r.ok ? std::string() : refusal_message(r));
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
        // Scenes at different rates are no longer refused (see test_subscene_rate_*): the old refusal is gone.
        const std::string text = project("video.rate=30\nvideo.scale=1\n[scene.1]\nscene=1\nvideo.rate=60\n"
                                         "video.scale=1\n",
                                         "");
        const RetimeResult r = retime_target(text, rate(66));
        check(r.ok && !contains(r.refusal_reason, "do not share one"), "accepts scenes with different rates",
              refusal_message(r));
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

// ---------------------------------------------------------------------------
// A small model of the host for the frame-by-frame checks: what a trackbar or a time control object shows at each
// frame of the rewritten project, read from the rewritten text only.
// ---------------------------------------------------------------------------

using LD = long double;
using std::int64_t;

std::vector<std::string> split_commas(const std::string& text) {
    std::vector<std::string> out;
    std::size_t start = 0;
    for (;;) {
        const std::size_t comma = text.find(',', start);
        if (comma == std::string::npos) {
            out.push_back(text.substr(start));
            break;
        }
        out.push_back(text.substr(start, comma - start));
        start = comma + 1;
    }
    return out;
}

std::vector<int64_t> parse_ints(const std::string& csv) {
    std::vector<int64_t> out;
    for (const std::string& part : split_commas(csv)) {
        out.push_back(std::stoll(part));
    }
    return out;
}

LD parse_number(const std::string& text) {
    return std::strtold(text.c_str(), nullptr);
}

// Digits after the decimal point of a plain number: "12.50" gives 2.
int decimal_places(const std::string& text) {
    const std::size_t dot = text.find('.');
    return dot == std::string::npos ? 0 : static_cast<int>(text.size() - dot - 1);
}

LD pow10_neg(int places) {
    LD v = 1;
    for (int i = 0; i < places; ++i) {
        v /= 10;
    }
    return v;
}

int64_t floor_div_i(int64_t n, int64_t d) {  // d > 0
    int64_t q = n / d;
    if (n % d != 0 && n < 0) {
        --q;
    }
    return q;
}

// key -> value of the first row with that key in the sections [obj] and [obj.N] of an .aup2 text.
std::map<std::string, std::string> object_rows(const std::string& text, int obj) {
    std::map<std::string, std::string> rows;
    std::istringstream in(text);
    std::string line;
    const std::string head = "[" + std::to_string(obj) + "]";
    const std::string effect_head = "[" + std::to_string(obj) + ".";
    bool inside = false;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        if (!line.empty() && line.front() == '[') {
            inside = line == head || line.compare(0, effect_head.size(), effect_head) == 0;
            continue;
        }
        const std::size_t eq = line.find('=');
        if (inside && eq != std::string::npos) {
            rows.emplace(line.substr(0, eq), line.substr(eq + 1));
        }
    }
    return rows;
}

// The text after '=' of a trackbar row, "v0,...,vn,mode,param": the values and the mode.
struct Track {
    std::vector<LD> values;
    std::string mode;
};

Track parse_track(const std::string& row_value) {
    const std::vector<std::string> parts = split_commas(row_value);
    Track track;
    track.mode = parts[parts.size() - 2];
    for (std::size_t i = 0; i + 2 < parts.size(); ++i) {
        track.values.push_back(parse_number(parts[i]));
    }
    return track;
}

// Value at frame n of straight lines through the keyframes; the last value after the last keyframe.
LD host_line_value(const std::vector<int64_t>& frames, const std::vector<LD>& values, int64_t n) {
    if (n <= frames.front()) {
        return values.front();
    }
    if (n >= frames.back()) {
        return values.back();
    }
    for (std::size_t i = 0; i + 1 < frames.size(); ++i) {
        if (n <= frames[i + 1]) {
            return values[i] + (values[i + 1] - values[i]) * static_cast<LD>(n - frames[i]) /
                                   static_cast<LD>(frames[i + 1] - frames[i]);
        }
    }
    return values.back();
}

void test_precise_fixtures() {
    const std::string t30 = read_file(fixture_dir + "\\freerenderfps_30fps.aup2");
    const RetimeResult ntsc = free_fps::retime(t30, rate(60000, 1001), L"D:\\out\\retimed.aup2", true);
    check(ntsc.ok, "precise 30->60000/1001: converts", refusal_message(ntsc));
    // The object ends at 149 and 149 x 2000/1001 = 297.7: the value is v(297 / k) at 297, then it stays at the end value.
    check(frame_line(ntsc, "frame=") == "frame=0,297,298,299",
          "precise 30->60000/1001: frame=0,297,298,299 (clamp points added)", frame_line(ntsc, "frame="));
    check(frame_line(ntsc, "X=") == "X=-200.00,199.06,200.00,200.00,直線移動,0",
          "precise 30->60000/1001: X stops at 200.00 (no 201.74)", frame_line(ntsc, "X="));
    check(frame_line(ntsc, "Y=") == "Y=0.00,0.00,0.00,0.00,直線移動,0",
          "precise 30->60000/1001: constant Y repeats its value at every keyframe", frame_line(ntsc, "Y="));
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
        check(x.end_extrapolation == "0.000000", "precise: X end shift is 0 (the last value is held), 6 decimals",
              x.end_extrapolation);
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
    check(extra.ok && frame_line(extra, "X=") == "X=-200.0000,199.0564,200.0000,200.0000,直線移動,0",
          "precise extra decimals 2: X has 4 decimals", frame_line(extra, "X="));

    const RetimeResult f2 = free_fps::retime_factor(t30, 2, L"D:\\out\\retimed.aup2", true);
    check(f2.ok && frame_line(f2, "frame=") == "frame=0,298,299" &&
              frame_line(f2, "X=") == "X=-200.00,200.00,200.00,直線移動,0",
          "precise factor 2: frame=0,298,299 and X holds 200.00 on the last frame (e k = 298 is an integer)",
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
    check(frame_line(p, "Z=") == "Z=0.00,50.00,50.00,直線移動,0", "precise: eligible object 1 is re-sampled and held",
          frame_line(p, "Z="));
    check(frame_line(p, "W=") == "W=0.00,100.00,100.00,直線移動,0", "precise: eligible object 2 is re-sampled and held",
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

// ---------------------------------------------------------------------------
// Scenes with different rates (SubsceneRatePolicy). The exported scene is display.scene.
// ---------------------------------------------------------------------------

using free_fps::ScenePlan;
using free_fps::SubsceneRatePolicy;

const SubsceneRatePolicy kAuto = SubsceneRatePolicy::Auto;
const SubsceneRatePolicy kFollow = SubsceneRatePolicy::Follow;
const SubsceneRatePolicy kKeep = SubsceneRatePolicy::Keep;

// One [scene.N] section followed by the objects that belong to it.
std::string scene_block(int no, const std::string& name, std::int64_t rate_value, std::int64_t scale_value,
                        const std::string& objects) {
    const std::string n = std::to_string(no);
    return "[scene." + n + "]\nscene=" + n + "\nname=" + name + "\nlabel=\nvideo.width=640\nvideo.height=360\n" +
           "video.rate=" + std::to_string(rate_value) + "\nvideo.scale=" + std::to_string(scale_value) +
           "\naudio.rate=44100\ncursor.frame=30\npreview.frame=60\ndisplay.frame=90\n" + objects;
}

std::string multi_scene_project(const std::string& project_lines, const std::string& scenes) {
    return "[project]\nversion=2011200\nfile=D:\\old\\src.aup2\n" + project_lines + scenes;
}

// Main-scene object that plays scene 1 (the host's シーン effect); its parameters are in seconds and percent.
const char* const kSceneEffect = "[0]\nlayer=0\nframe=0,149\n[0.0]\neffect.name=シーン\n"
                                 "再生位置=0.000,5.000,再生範囲,0\n再生速度=100.00\nシーン=1\nループ再生=0\n";
const char* const kSceneEffectLines = "再生位置=0.000,5.000,再生範囲,0\n再生速度=100.00\nシーン=1\nループ再生=0\n";

std::string sub_object(int object_no, int scene_no, const std::string& last_frame) {
    return "[" + std::to_string(object_no) + "]\nlayer=0\nscene=" + std::to_string(scene_no) + "\nframe=0," +
           last_frame + "\n[" + std::to_string(object_no) + ".0]\neffect.name=標準描画\nX=0.00,100.00,直線移動,0\n";
}

// Main scene 0 at 30 FPS, sub scene 1 at sub_rate (5 seconds of content each), display.scene as given.
std::string main_and_sub(std::int64_t sub_rate, const std::string& last_frame,
                         const std::string& project_lines = "display.scene=0\npreview.scene=0\n") {
    return multi_scene_project(project_lines, scene_block(0, "主合成", 30, 1, kSceneEffect) +
                                                  scene_block(1, "次合成1", sub_rate, 1, sub_object(1, 1, last_frame)));
}

// The text from the first occurrence of marker to the end (the last scene section and its objects).
std::string from_marker(const std::string& text, const std::string& marker) {
    const std::size_t pos = text.find(marker);
    return pos == std::string::npos ? std::string() : text.substr(pos);
}

std::string before_marker(const std::string& text, const std::string& marker) {
    const std::size_t pos = text.find(marker);
    return pos == std::string::npos ? text : text.substr(0, pos);
}

RetimeResult run_policy(const std::string& text, Rational target, SubsceneRatePolicy policy, bool precise = false,
                        std::size_t extra = 0) {
    return free_fps::retime(text, target, L"D:\\out\\retimed.aup2", precise, extra, policy);
}

bool plan_is(const ScenePlan& p, int scene, const std::string& name, const std::string& source,
             const std::string& target, const std::string& k, bool follow, bool exported) {
    return p.scene == scene && p.name == name && p.source_rate == source && p.target_rate == target && p.k == k &&
           p.follow == follow && p.exported == exported;
}

std::string plan_text(const RetimeResult& r) {
    std::string out;
    for (const ScenePlan& p : r.scenes) {
        out += "[" + std::to_string(p.scene) + " " + p.name + " " + p.source_rate + "->" + p.target_rate + " k=" + p.k +
               (p.follow ? " follow" : " keep") + (p.exported ? " exported" : "") + "] ";
    }
    return out;
}

// Main 30 FPS + sub 60 FPS, target 60: the sub scene is already at the target (auto follows it, k = 1).
void test_subscene_main30_sub60() {
    const std::string text = main_and_sub(60, "299");
    for (SubsceneRatePolicy policy : {kAuto, kFollow}) {
        const std::string tag = policy == kAuto ? "auto" : "follow";
        const RetimeResult r = run_policy(text, rate(60), policy);
        check(r.ok, "sub60 " + tag + " 30->60: converts", refusal_message(r));
        if (!r.ok) {
            continue;
        }
        check(r.subscene_rate == tag && r.source_rate == "30/1" && r.target_rate == "60/1" && r.k == "2/1",
              "sub60 " + tag + ": top level is the exported scene (30/1 -> 60/1, k 2/1)");
        check(r.scenes.size() == 2 && plan_is(r.scenes[0], 0, "主合成", "30/1", "60/1", "2/1", true, true) &&
                  plan_is(r.scenes[1], 1, "次合成1", "60/1", "60/1", "1/1", true, false),
              "sub60 " + tag + ": scene plan (main follows k 2/1, sub follows k 1/1)", plan_text(r));
        const std::string head = before_marker(r.output, "[scene.1]");
        const std::string tail = from_marker(r.output, "[scene.1]");
        check(contains(head, "[0]\nlayer=0\nframe=0,299\n") && contains(head, "cursor.frame=60\n") &&
                  contains(head, "video.rate=60\nvideo.scale=1\n"),
              "sub60 " + tag + ": main scene rescaled by 2 (frame=0,299, cursor.frame=60, rate 60)");
        check(contains(tail, "scene=1\nframe=0,299\n") && contains(tail, "cursor.frame=30\n") &&
                  contains(tail, "video.rate=60\nvideo.scale=1\n"),
              "sub60 " + tag + ": sub scene rescaled by 1 (frame=0,299, cursor.frame=30, rate 60)");
        check(contains(head, std::string(kSceneEffectLines)),
              "sub60 " + tag + ": シーン parameters (seconds, percent, scene number, loop) are unchanged");
        check(r.objects.size() == 2, "sub60 " + tag + ": both objects are in the timing report");
        check(r.notes.empty(), "sub60 " + tag + ": no notes");
    }
    const RetimeResult keep = run_policy(text, rate(60), kKeep);
    check(keep.ok && keep.subscene_rate == "keep", "sub60 keep 30->60: converts", refusal_message(keep));
    check(keep.scenes.size() == 2 && plan_is(keep.scenes[0], 0, "主合成", "30/1", "60/1", "2/1", true, true) &&
              plan_is(keep.scenes[1], 1, "次合成1", "60/1", "60/1", "1/1", false, false),
          "sub60 keep: sub is kept (target = source, k 1/1)", plan_text(keep));
    check(from_marker(keep.output, "[scene.1]") == from_marker(text, "[scene.1]"),
          "sub60 keep: the sub scene section and its objects are byte for byte unchanged");
    check(keep.notes.size() == 1 && keep.notes[0] == "scene 1 kept at its own rate 60/1",
          "sub60 keep: note names the kept scene", keep.notes.empty() ? "" : keep.notes[0]);
    check(keep.objects.size() == 1, "sub60 keep: only the main object is in the timing report");

    // Target 120: main k = 4, sub k = 2 (follow) or untouched (keep).
    const RetimeResult f120 = run_policy(text, rate(120), kAuto);
    check(f120.ok && f120.scenes.size() == 2 && plan_is(f120.scenes[0], 0, "主合成", "30/1", "120/1", "4/1", true, true) &&
              plan_is(f120.scenes[1], 1, "次合成1", "60/1", "120/1", "2/1", true, false),
          "sub60 auto 30->120: main k 4/1, sub k 2/1", plan_text(f120));
    check(f120.ok && contains(before_marker(f120.output, "[scene.1]"), "frame=0,599\n") &&
              contains(from_marker(f120.output, "[scene.1]"), "scene=1\nframe=0,599\n") &&
              contains(from_marker(f120.output, "[scene.1]"), "cursor.frame=60\n") &&
              contains(from_marker(f120.output, "[scene.1]"), "video.rate=120\nvideo.scale=1\n"),
          "sub60 auto 30->120: both scenes reach 120 FPS and 5 s (frame=0,599)");
    const RetimeResult k120 = run_policy(text, rate(120), kKeep);
    check(k120.ok && from_marker(k120.output, "[scene.1]") == from_marker(text, "[scene.1]") &&
              contains(before_marker(k120.output, "[scene.1]"), "video.rate=120\n"),
          "sub60 keep 30->120: sub untouched, main at 120");

    // Sidecar layout: subscene_rate and scenes right after k, one object per scene with the fixed key order.
    const RetimeResult r = run_policy(text, rate(60), kAuto);
    const std::string json = free_fps::sidecar_json(r, L"D:\\in.aup2", L"D:\\out\\retimed.aup2");
    check(contains(json, "  \"k\": \"2/1\",\n  \"subscene_rate\": \"auto\",\n  \"scenes\": [\n    {\n"
                         "      \"scene\": 0,\n      \"name\": \"主合成\",\n      \"source_rate\": \"30/1\",\n"
                         "      \"target_rate\": \"60/1\",\n      \"k\": \"2/1\",\n      \"policy\": \"follow\",\n"
                         "      \"exported\": true\n    },\n    {\n      \"scene\": 1,\n      \"name\": \"次合成1\",\n"
                         "      \"source_rate\": \"60/1\",\n      \"target_rate\": \"60/1\",\n      \"k\": \"1/1\",\n"
                         "      \"policy\": \"follow\",\n      \"exported\": false\n    }\n  ],\n  \"input\": {\n"),
          "sub60 sidecar: subscene_rate and scenes sit between k and input, in the specified key order");
}

// Main 30 FPS + sub 15 FPS, target 60: auto treats the slower scene as deliberate frame dropping.
void test_subscene_main30_sub15() {
    const std::string text = main_and_sub(15, "74");
    const RetimeResult autop = run_policy(text, rate(60), kAuto);
    check(autop.ok, "sub15 auto 30->60: converts", refusal_message(autop));
    check(autop.scenes.size() == 2 && plan_is(autop.scenes[0], 0, "主合成", "30/1", "60/1", "2/1", true, true) &&
              plan_is(autop.scenes[1], 1, "次合成1", "15/1", "15/1", "1/1", false, false),
          "sub15 auto: sub scene is kept at 15/1", plan_text(autop));
    check(from_marker(autop.output, "[scene.1]") == from_marker(text, "[scene.1]"),
          "sub15 auto: the sub scene section and its objects are byte for byte unchanged");
    check(contains(before_marker(autop.output, "[scene.1]"), "frame=0,299\n"), "sub15 auto: main rescaled by 2");
    check(autop.notes.size() == 1 && autop.notes[0] == "scene 1 kept at its own rate 15/1",
          "sub15 auto: note 'scene 1 kept at its own rate 15/1'", autop.notes.empty() ? "" : autop.notes[0]);
    check(autop.objects.size() == 1, "sub15 auto: kept object is not in the timing report");
    const std::string json = free_fps::sidecar_json(autop, L"D:\\in.aup2", L"D:\\out\\retimed.aup2");
    check(contains(json, "      \"scene\": 1,\n      \"name\": \"次合成1\",\n      \"source_rate\": \"15/1\",\n"
                         "      \"target_rate\": \"15/1\",\n      \"k\": \"1/1\",\n      \"policy\": \"keep\",\n"
                         "      \"exported\": false\n"),
          "sub15 auto sidecar: kept scene has target_rate = source_rate, k 1/1, policy keep");

    const RetimeResult keep = run_policy(text, rate(60), kKeep);
    check(keep.ok && keep.output == autop.output && keep.notes == autop.notes,
          "sub15 keep: same .aup2 and notes as auto (the slower scene is kept either way)");

    const RetimeResult follow = run_policy(text, rate(60), kFollow);
    check(follow.ok && follow.scenes.size() == 2 &&
              plan_is(follow.scenes[1], 1, "次合成1", "15/1", "60/1", "4/1", true, false),
          "sub15 follow: sub scene follows with k 4/1", plan_text(follow));
    const std::string tail = from_marker(follow.output, "[scene.1]");
    check(follow.ok && contains(tail, "scene=1\nframe=0,299\n") && contains(tail, "cursor.frame=120\n") &&
              contains(tail, "video.rate=60\nvideo.scale=1\n") && follow.notes.empty(),
          "sub15 follow: sub frame=0,74 -> 0,299 (5 s), cursor.frame 30 -> 120, rate 60, no keep note");
    check(follow.ok && follow.objects.size() == 2, "sub15 follow: both objects are in the timing report");

    // Precise mode uses each scene's own k. The sub scene's old frames are 0,74; with k = 4 the keyframes are 0, 296
    // (74 x 4) and 299: X = 100 at 296 and it stays there. With the main scene's k = 2 they would be 0, 148 and 149.
    const RetimeResult pf = run_policy(text, rate(60), kFollow, true);
    check(pf.ok && pf.precise && pf.precise->applied == 1 && pf.precise->skipped.empty() &&
              pf.precise->tracks.size() == 1 &&
              contains(from_marker(pf.output, "[scene.1]"), "frame=0,296,299\n") &&
              contains(from_marker(pf.output, "[scene.1]"), "X=0.00,100.00,100.00,直線移動,0\n"),
          "sub15 follow precise: the sub scene's trackbar is re-sampled with the sub scene's k 4/1", refusal_message(pf));
    check(pf.ok && contains(before_marker(pf.output, "[scene.1]"), std::string(kSceneEffectLines)),
          "sub15 follow precise: シーン row 再生位置=...,再生範囲,0 is still left alone");
    const RetimeResult pk = run_policy(text, rate(60), kAuto, true);
    check(pk.ok && pk.precise && pk.precise->applied == 0 && pk.precise->skipped.empty() && pk.precise->tracks.empty() &&
              from_marker(pk.output, "[scene.1]") == from_marker(text, "[scene.1]"),
          "sub15 auto precise: a kept scene is neither re-sampled nor recorded as skipped", refusal_message(pk));

    // Factor mode: target = rate of the exported scene * factor.
    const RetimeResult f2 = free_fps::retime_factor(text, 2, L"D:\\out\\retimed.aup2", false, 0, kAuto);
    check(f2.ok && f2.target_rate == "60/1" && f2.k == "2/1" && f2.factor && *f2.factor == 2 && f2.scenes.size() == 2 &&
              !f2.scenes[1].follow && from_marker(f2.output, "[scene.1]") == from_marker(text, "[scene.1]"),
          "sub15 factor 2 auto: target 60/1, main k 2/1, sub kept", refusal_message(f2));
    const RetimeResult f2f = free_fps::retime_factor(text, 2, L"D:\\out\\retimed.aup2", false, 0, kFollow);
    check(f2f.ok && f2f.scenes.size() == 2 && plan_is(f2f.scenes[1], 1, "次合成1", "15/1", "60/1", "4/1", true, false),
          "sub15 factor 2 follow: sub follows with k 4/1", plan_text(f2f));
}

// display.scene decides the exported scene and with it the rate everything is compared with.
void test_subscene_display_scene() {
    const std::string text60 = main_and_sub(60, "299", "display.scene=1\ndisplay.scene.list.label=\npreview.scene=0\n");
    // Exported scene 1 (60 FPS), target 60: scene 0 is slower (30 < 60), so auto keeps it.
    const RetimeResult autop = run_policy(text60, rate(60), kAuto);
    check(autop.ok, "display.scene=1 auto 60->60: converts", refusal_message(autop));
    check(autop.source_rate == "60/1" && autop.target_rate == "60/1" && autop.k == "1/1",
          "display.scene=1: top-level source_rate/k are the exported scene's (60/1, k 1/1)",
          autop.source_rate + " " + autop.target_rate + " " + autop.k);
    check(autop.scenes.size() == 2 && plan_is(autop.scenes[0], 0, "主合成", "30/1", "30/1", "1/1", false, false) &&
              plan_is(autop.scenes[1], 1, "次合成1", "60/1", "60/1", "1/1", true, true),
          "display.scene=1 auto: scene 0 kept, scene 1 exported", plan_text(autop));
    check(autop.changes.size() == 1 && autop.changes[0].new_text == "file=D:\\out\\retimed.aup2",
          "display.scene=1 auto 60->60: only file= changes (nothing is retimed)", std::to_string(autop.changes.size()));
    check(autop.notes.size() == 1 && autop.notes[0] == "scene 0 kept at its own rate 30/1", "display.scene=1: keep note");

    const RetimeResult follow = run_policy(text60, rate(120), kFollow);
    check(follow.ok && follow.k == "2/1" && follow.scenes.size() == 2 &&
              plan_is(follow.scenes[0], 0, "主合成", "30/1", "120/1", "4/1", true, false) &&
              plan_is(follow.scenes[1], 1, "次合成1", "60/1", "120/1", "2/1", true, true),
          "display.scene=1 follow 60->120: scene 0 k 4/1, exported scene 1 k 2/1", plan_text(follow));
    const RetimeResult fac = free_fps::retime_factor(text60, 2, L"D:\\out\\retimed.aup2", false, 0, kFollow);
    check(fac.ok && fac.target_rate == "120/1" && fac.scenes.size() == 2 && fac.scenes[0].k == "4/1",
          "display.scene=1 factor 2 follow: target = 60 * 2 = 120/1", plan_text(fac));

    // display.scene that has no [scene.N].
    {
        const std::string missing = main_and_sub(60, "299", "display.scene=5\n");
        const RetimeResult r = run_policy(missing, rate(60), kAuto);
        check(!r.ok && r.refusal_line == 0 && r.refusal_reason == "display.scene 5 does not exist" &&
                  free_fps::refusal_message(r) == "REFUSED: display.scene 5 does not exist" && r.scenes.empty(),
              "display.scene=5 with no scene 5: refused", r.ok ? "converted" : free_fps::refusal_message(r));
        const RetimeResult rk = run_policy(missing, rate(60), kKeep);
        check(!rk.ok && rk.refusal_reason == "display.scene 5 does not exist", "display.scene=5: refused under keep too");
    }
    {
        // No display.scene line: 0 is assumed, and only scene 1 exists.
        const std::string only1 = multi_scene_project("", scene_block(1, "x", 30, 1, sub_object(0, 1, "29")));
        const RetimeResult r = run_policy(only1, rate(60), kAuto);
        check(!r.ok && r.refusal_reason == "display.scene 0 does not exist",
              "no display.scene and no scene 0: refused as display.scene 0", r.ok ? "converted" : free_fps::refusal_message(r));
        // And with display.scene=1 the same file converts.
        const std::string ok1 = multi_scene_project("display.scene=1\n", scene_block(1, "x", 30, 1, sub_object(0, 1, "29")));
        const RetimeResult ok = run_policy(ok1, rate(60), kAuto);
        check(ok.ok && contains(ok.output, "frame=0,59\n") && ok.scenes.size() == 1 && ok.scenes[0].exported,
              "display.scene=1 with only scene 1: converts", refusal_message(ok));
    }
    {
        const std::string bad = main_and_sub(60, "299", "display.scene=abc\n");
        const RetimeResult r = run_policy(bad, rate(60), kAuto);
        check(!r.ok && r.refusal_line == 4 &&
                  free_fps::refusal_message(r) ==
                      "REFUSED: line 4: display.scene must be a non-negative integer, got 'abc'",
              "display.scene=abc: refused as not a non-negative integer",
              r.ok ? "converted" : free_fps::refusal_message(r));
    }
    {
        // The first display.scene line counts.
        const std::string two = main_and_sub(60, "299", "display.scene=1\ndisplay.scene=0\n");
        const RetimeResult r = run_policy(two, rate(60), kAuto);
        check(r.ok && r.source_rate == "60/1" && r.scenes.size() == 2 && r.scenes[1].exported && !r.scenes[0].exported,
              "display.scene: the first line wins", refusal_message(r));
    }
}

// One scene: every policy gives the old output, byte for byte. The SHA-256 values are those of the output of the
// version before subscene_rate existed (built from the previous commit with the same inputs and output path), except
// the precise 30->60000/1001 value and the two-scene precise value below: they are the outputs of the version that holds
// the last value of an object (the new object ends after the original end there; 66->24 does not and is unchanged).
void test_subscene_single_scene_unchanged() {
    const std::string t30 = to_lf(read_file(fixture_dir + "\\freerenderfps_30fps.aup2"));
    const std::string t66 = to_lf(read_file(fixture_dir + "\\freerenderfps_66fps.aup2"));
    const std::string t120 = to_lf(read_file(fixture_dir + "\\freerenderfps_120fps.aup2"));
    struct Golden {
        const char* name;
        RetimeResult (*run)(const std::string&, SubsceneRatePolicy);
        const std::string* input;
        const char* sha256;
    };
    const Golden cases[] = {
        {"fixture 30->66", [](const std::string& t, SubsceneRatePolicy p) { return free_fps::retime(t, rate(66), L"D:\\out\\retimed.aup2", false, 0, p); },
         &t30, "93e2d4d52a605bedec55277e560e865f75db8a652d17c89a0af6fdb0ef407421"},
        {"fixture 30->60000/1001 precise", [](const std::string& t, SubsceneRatePolicy p) { return free_fps::retime(t, rate(60000, 1001), L"D:\\out\\retimed.aup2", true, 0, p); },
         &t30, "e8b08f894a6eaafe296378625c0381cf9f0b3ec1dbd8b42df91e731f28132a28"},
        {"fixture 30 factor 2", [](const std::string& t, SubsceneRatePolicy p) { return free_fps::retime_factor(t, 2, L"D:\\out\\retimed.aup2", false, 0, p); },
         &t30, "2e848c89016318c7022f9618c1f4529b0ea49f5cfb27bab70f8652fa72babe6f"},
        {"fixture 66->24 precise", [](const std::string& t, SubsceneRatePolicy p) { return free_fps::retime(t, rate(24), L"D:\\out\\retimed.aup2", true, 0, p); },
         &t66, "a9dd9547ee45c27f1bfd561f2cfb2891cf6c517d27c7777b59921323746dac3a"},
        {"fixture 66->24 precise +2 decimals", [](const std::string& t, SubsceneRatePolicy p) { return free_fps::retime(t, rate(24), L"D:\\out\\retimed.aup2", true, 2, p); },
         &t66, "7c5c75b04487004a95dafe43d13c4d07345cce9c7a22edbc8e4538c83464c2ac"},
        {"fixture 120->60", [](const std::string& t, SubsceneRatePolicy p) { return free_fps::retime(t, rate(60), L"D:\\out\\retimed.aup2", false, 0, p); },
         &t120, "03975b14a2ef3c95798a82c6ad291afce08e56c5b04a626279b52701ed59d096"},
    };
    for (const Golden& g : cases) {
        for (SubsceneRatePolicy policy : {kAuto, kFollow, kKeep}) {
            const RetimeResult r = g.run(*g.input, policy);
            const std::string tag = std::string(g.name) + (policy == kAuto ? " auto" : policy == kFollow ? " follow" : " keep");
            check(r.ok && r.output_sha256 == g.sha256, "single scene " + tag + ": .aup2 identical to the previous version",
                  r.ok ? r.output_sha256 : refusal_message(r));
            check(r.ok && r.scenes.size() == 1 && r.scenes[0].follow && r.scenes[0].exported && r.scenes[0].k == r.k &&
                      r.scenes[0].source_rate == r.source_rate && r.scenes[0].target_rate == r.target_rate,
                  "single scene " + tag + ": one scene in the plan, exported and following", plan_text(r));
            bool keep_note = false;
            for (const std::string& note : r.notes) {
                keep_note = keep_note || contains(note, "kept at its own rate");
            }
            check(r.ok && !keep_note, "single scene " + tag + ": no keep note");
        }
    }

    // Two scenes at one rate: auto and follow are the previous version's output; keep leaves scene 1 alone.
    const std::string two =
        "[project]\nversion=2011200\nfile=D:\\old\\src.aup2\n"
        "[scene.0]\nscene=0\nname=main\nvideo.width=640\nvideo.height=360\nvideo.rate=30\nvideo.scale=1\naudio.rate=44100\n"
        "cursor.frame=30\npreview.frame=60\ndisplay.frame=90\n"
        "[0]\nlayer=0\nframe=0,149\n[0.0]\neffect.name=標準描画\nX=0.00,100.00,直線移動,0\n"
        "[scene.1]\nscene=1\nname=sub\nvideo.width=640\nvideo.height=360\nvideo.rate=30\nvideo.scale=1\naudio.rate=44100\n"
        "cursor.frame=15\npreview.frame=45\ndisplay.frame=75\n"
        "[1]\nlayer=0\nscene=1\nframe=0,89\n[1.0]\neffect.name=標準描画\nX=0.00,50.00,直線移動,0\n";
    for (SubsceneRatePolicy policy : {kAuto, kFollow}) {
        const std::string tag = policy == kAuto ? "auto" : "follow";
        const RetimeResult plain = run_policy(two, rate(66), policy);
        const RetimeResult precise = run_policy(two, rate(66), policy, true);
        const RetimeResult fac = free_fps::retime_factor(two, 3, L"D:\\out\\retimed.aup2", false, 0, policy);
        check(plain.ok && plain.output_sha256 == "c7bc22174a37ce8de028990fa035a08c87bbddfa8394ebe1be75f5e939054d87",
              "two scenes at one rate " + tag + " 30->66: .aup2 identical to the previous version", plain.output_sha256);
        check(precise.ok && precise.output_sha256 == "2c0801aaee833be21650617ea3dab95fdd0a48920b066a85a4a30d2259f9bb9d",
              "two scenes at one rate " + tag + " precise 30->66: .aup2 is the end-hold version's output",
              precise.output_sha256);
        check(fac.ok && fac.output_sha256 == "0aabf9e138a7e93a9499cd8606d79e92cc6fa26c0d8c6962a3be5af8981d9f0b",
              "two scenes at one rate " + tag + " factor 3: .aup2 identical to the previous version", fac.output_sha256);
        check(plain.ok && plain.notes.empty() && plain.scenes.size() == 2 && plain.scenes[0].follow &&
                  plain.scenes[1].follow,
              "two scenes at one rate " + tag + ": both follow, no notes", plan_text(plain));
    }
    const RetimeResult keep = run_policy(two, rate(66), kKeep);
    check(keep.ok && from_marker(keep.output, "[scene.1]") == from_marker(two, "[scene.1]") &&
              contains(before_marker(keep.output, "[scene.1]"), "frame=0,329\n") && keep.notes.size() == 1 &&
              keep.notes[0] == "scene 1 kept at its own rate 30/1",
          "two scenes at one rate keep: scene 1 untouched, scene 0 retimed", refusal_message(keep));
}

// The plan and the notes are in scene-number order (not file order); policy compares rates as exact fractions.
void test_subscene_plan_order_and_comparison() {
    // File order 0, 2, 1, 3. Rates: scene 0 = 30, scene 2 = 15, scene 1 = 60, scene 3 = 30.
    const std::string text = multi_scene_project(
        "display.scene=0\n", scene_block(0, "a", 30, 1, sub_object(0, 0, "29")) +
                                 scene_block(2, "c", 15, 1, sub_object(2, 2, "14")) +
                                 scene_block(1, "b", 60, 1, sub_object(1, 1, "59")) +
                                 scene_block(3, "", 30, 1, sub_object(3, 3, "29")));
    const RetimeResult r = run_policy(text, rate(60), kAuto);
    check(r.ok && r.scenes.size() == 4 && plan_is(r.scenes[0], 0, "a", "30/1", "60/1", "2/1", true, true) &&
              plan_is(r.scenes[1], 1, "b", "60/1", "60/1", "1/1", true, false) &&
              plan_is(r.scenes[2], 2, "c", "15/1", "15/1", "1/1", false, false) &&
              plan_is(r.scenes[3], 3, "", "30/1", "60/1", "2/1", true, false),
          "plan order: sorted by scene number although the file has 0,2,1,3; 15 kept, 60 and 30 follow; empty name stays empty",
          plan_text(r));
    check(r.ok && r.notes.size() == 1 && r.notes[0] == "scene 2 kept at its own rate 15/1", "plan order: one keep note");
    check(r.ok && free_fps::sidecar_json(r, L"D:\\in.aup2", L"D:\\out\\retimed.aup2").find("\"name\": \"\",") != std::string::npos,
          "plan order: a scene without name= gets an empty name in the sidecar");

    // 30000/1001 main: a 30/1 scene is faster (follows), 2997/100 is slower (kept), 30000/1001 itself follows.
    struct Case {
        std::int64_t rate_value;
        std::int64_t scale_value;
        bool follow;
    };
    const Case cases[] = {{30, 1, true}, {2997, 100, false}, {30000, 1001, true}, {60000, 2002, true}, {29, 1, false}};
    for (const Case& c : cases) {
        const std::string t = multi_scene_project(
            "display.scene=0\n", scene_block(0, "m", 30000, 1001, sub_object(0, 0, "29")) +
                                     scene_block(1, "s", c.rate_value, c.scale_value, sub_object(1, 1, "29")));
        const RetimeResult x = run_policy(t, rate(60), kAuto);
        check(x.ok && x.scenes.size() == 2 && x.scenes[1].follow == c.follow,
              "auto compares exact fractions: " + std::to_string(c.rate_value) + "/" + std::to_string(c.scale_value) +
                  " against 30000/1001 " + (c.follow ? "follows" : "is kept"),
              plan_text(x));
    }
}

// A refusal that comes after the scenes were read still returns the plan; a kept scene is not validated by rescaling.
void test_subscene_refusal_keeps_plan_and_keep_is_untouched() {
    // Scene 1 (15 FPS) has an object whose frame= end is before its start.
    const std::string bad_sub = multi_scene_project(
        "display.scene=0\n", scene_block(0, "主合成", 30, 1, kSceneEffect) +
                                 scene_block(1, "次合成1", 15, 1, "[1]\nlayer=0\nscene=1\nframe=5,3\n"));
    const RetimeResult f = run_policy(bad_sub, rate(60), kFollow);
    check(!f.ok && contains(f.refusal_reason, "is before start") && f.scenes.size() == 2 &&
              f.subscene_rate == "follow" && plan_is(f.scenes[1], 1, "次合成1", "15/1", "60/1", "4/1", true, false),
          "refusal after the scenes were read: the plan is still returned", plan_text(f));
    const RetimeResult k = run_policy(bad_sub, rate(60), kAuto);
    check(k.ok && from_marker(k.output, "[scene.1]") == from_marker(bad_sub, "[scene.1]"),
          "auto keeps the 15 FPS scene: its objects are not rescaled, so its frame= is not refused", refusal_message(k));
    // The other refusals are unchanged: a malformed scene key is still refused in every scene.
    const std::string bad_key = multi_scene_project(
        "display.scene=0\n", scene_block(0, "m", 30, 1, kSceneEffect) +
                                 scene_block(1, "s", 15, 1, "keyframe_hint=1\n"));
    const RetimeResult key = run_policy(bad_key, rate(60), kAuto);
    check(!key.ok && contains(key.refusal_reason, "keyframe_hint"), "unknown frame/mark/range scene key is refused in any scene");
}

// A kept scene is opaque: nothing in it is validated except the scene section keys, a duplicate frame= and the
// object scene= value.
void test_subscene_kept_scene_is_opaque() {
    const std::string bad_objects =
        "[1]\nlayer=0\nscene=1\nframe=0,x\n[1.0]\neffect.name=標準描画\nX=0,1,2,謎移動,0|27\nY=obj.frame\n"
        "[2]\nlayer=0\nscene=1\nframe=0,9\n[2.0]\neffect.name=カウンター\n[3]\nlayer=0\nscene=1\nframe=0,9\n";
    const std::string text = multi_scene_project("display.scene=0\n", scene_block(0, "m", 30, 1, kSceneEffect) +
                                                                           scene_block(1, "s", 15, 1, bad_objects));
    const RetimeResult autop = run_policy(text, rate(60), kAuto);
    check(autop.ok && from_marker(autop.output, "[scene.1]") == from_marker(text, "[scene.1]"),
          "kept scene: bad frame=, unknown trackbar mode, '|' payload, obj.frame and a counter without 速度 are not looked at",
          refusal_message(autop));
    check(autop.ok && autop.notes.size() == 1, "kept scene: no counter note from a kept scene",
          autop.notes.empty() ? "" : autop.notes[0]);
    const RetimeResult follow = run_policy(text, rate(60), kFollow);
    check(!follow.ok && contains(follow.refusal_reason, "frame= must be 2 or more"),
          "the same scene followed: the bad frame= is refused", follow.ok ? "converted" : free_fps::refusal_message(follow));

    // Still checked in a kept scene: a duplicate frame= and a non-integer object scene=.
    const std::string dup = multi_scene_project(
        "display.scene=0\n", scene_block(0, "m", 30, 1, kSceneEffect) +
                                 scene_block(1, "s", 15, 1, "[1]\nlayer=0\nscene=1\nframe=0,9\nframe=0,9\n"));
    const RetimeResult d = run_policy(dup, rate(60), kAuto);
    check(!d.ok && contains(d.refusal_reason, "duplicate frame="), "kept scene: duplicate frame= is still refused");
    const std::string bad_scene = multi_scene_project(
        "display.scene=0\n", scene_block(0, "m", 30, 1, kSceneEffect) +
                                 scene_block(1, "s", 15, 1, "[1]\nlayer=0\nscene=x\nframe=0,9\n"));
    const RetimeResult b = run_policy(bad_scene, rate(60), kAuto);
    check(!b.ok && contains(b.refusal_reason, "object scene= must be an integer"),
          "kept scene: a non-integer object scene= is still refused");

    // A frame-dependent script is refused in a followed scene only.
    const std::string script = multi_scene_project(
        "display.scene=0\n", scene_block(0, "m", 30, 1, kSceneEffect) +
                                 scene_block(1, "s", 15, 1, "[1]\nlayer=0\nscene=1\nframe=0,9\n[1.0]\neffect.name=標準描画\nY=obj.frame\n"));
    check(run_policy(script, rate(60), kAuto).ok && !run_policy(script, rate(60), kFollow).ok,
          "obj.frame in the 15 FPS scene: accepted when kept (auto), refused when followed");

    // A kept scene without video.scale: read as 1 for the comparison, nothing inserted, no 'written after' note.
    const std::string no_scale = multi_scene_project(
        "display.scene=0\n", scene_block(0, "m", 30, 1, kSceneEffect) +
                                 "[scene.1]\nscene=1\nname=s\nvideo.rate=15\naudio.rate=44100\n" + sub_object(1, 1, "74"));
    const RetimeResult ns = run_policy(no_scale, rate(60), kAuto);
    check(ns.ok && !ns.scenes[1].follow && ns.scenes[1].source_rate == "15/1" &&
              from_marker(ns.output, "[scene.1]") == from_marker(no_scale, "[scene.1]") && ns.notes.size() == 1 &&
              ns.notes[0] == "scene 1 kept at its own rate 15/1",
          "kept scene without video.scale: not rewritten, only the keep note", refusal_message(ns));
    const RetimeResult nsf = run_policy(no_scale, rate(60), kFollow);
    check(nsf.ok && contains(from_marker(nsf.output, "[scene.1]"), "video.rate=60\nvideo.scale=1\n") &&
              nsf.notes.size() == 1 && nsf.notes[0] == "[scene.1] has no video.scale; read as 1 and written after video.rate",
          "followed scene without video.scale: video.scale inserted, with the note", refusal_message(nsf));
}

// コマ落ち other than 1 is refused in a followed scene, and on lines outside scenes/objects/effects, but not in a kept scene.
void test_subscene_column_drop_refusal() {
    const std::string kept = multi_scene_project(
        "display.scene=0\n", scene_block(0, "m", 30, 1, kSceneEffect) +
                                 scene_block(1, "s", 15, 1, "コマ落ち=2\n" + sub_object(1, 1, "74") + "コマ落ち=2\n"));
    const RetimeResult a = run_policy(kept, rate(60), kAuto);
    check(a.ok && from_marker(a.output, "[scene.1]") == from_marker(kept, "[scene.1]"),
          "コマ落ち=2 in a kept scene (section line, object, effect) is not refused", refusal_message(a));
    const RetimeResult f = run_policy(kept, rate(60), kFollow);
    check(!f.ok && contains(f.refusal_reason, "コマ落ち=2") && f.refusal_line == line_of_first(kept, "コマ落ち=2"),
          "コマ落ち=2 in the same scene when followed: refused at the first such line", refusal_message(f));
    const std::string in_main = multi_scene_project(
        "display.scene=0\n", scene_block(0, "m", 30, 1, std::string(kSceneEffect) + "コマ落ち=2\n") +
                                 scene_block(1, "s", 15, 1, sub_object(1, 1, "74")));
    check(!run_policy(in_main, rate(60), kAuto).ok, "コマ落ち=2 in the exported scene is refused");
    const std::string in_project = multi_scene_project("display.scene=0\nコマ落ち=2\n",
                                                       scene_block(0, "m", 30, 1, kSceneEffect) +
                                                           scene_block(1, "s", 15, 1, sub_object(1, 1, "74")));
    check(!run_policy(in_project, rate(60), kKeep).ok, "コマ落ち=2 in [project] is always refused");
    const RetimeResult plan = run_policy(in_main, rate(60), kAuto);
    check(plan.scenes.size() == 2, "コマ落ち=2 refusal: the scene plan is already available", plan_text(plan));
}

// The first name= line is the scene's name; of two video.rate lines the later one counts.
void test_subscene_first_name_and_duplicate_rate_lines() {
    const std::string text = multi_scene_project(
        "display.scene=0\n", scene_block(0, "first", 30, 1, kSceneEffect) +
                                 "[scene.1]\nscene=1\nname=a\nname=b\nvideo.rate=7\nvideo.rate=60\nvideo.scale=1\n" +
                                 sub_object(1, 1, "299"));
    const RetimeResult r = run_policy(text, rate(60), kAuto);
    check(r.ok && r.scenes.size() == 2 && r.scenes[1].name == "a" && r.scenes[1].source_rate == "60/1" &&
              r.scenes[1].follow,
          "first name= is used; of two video.rate lines the later one counts", plan_text(r));
}

// ---------------------------------------------------------------------------
// 時間制御(オブジェクト) and コマ落ち. The rewritten object is checked frame by frame against a model of the host
// (see tc_host_time): at every new frame it must show the controlled time the original shows at the same moment,
//   tau(f) = s + p(f) / 100 (L - 1),  p'(n) = 100 (k tau(g(clamp(n / k, s, e))) - s') / (L' - 1),
// rounded half away from zero to the decimals of the row. The literal rows below are those values written out.
// ---------------------------------------------------------------------------

const char* const kTcMode = "直線移動(時間制御)";

std::string tc_row(const std::string& a, const std::string& b, const std::string& flag = "0") {
    return a + "," + b + "," + kTcMode + "," + flag;
}

// A time control object on `layer` (the host controls the layers below it) with its 位置 row, 繰り返し, コマ落ち and
// 対象レイヤー数. scene_line is "scene=1\n" for an object of a sub scene.
std::string tc_object(int no, int layer, const std::string& frames, const std::string& row,
                      const std::string& drop = "1", const std::string& scene_line = std::string()) {
    const std::string n = std::to_string(no);
    return "[" + n + "]\nlayer=" + std::to_string(layer) + "\n" + scene_line + "frame=" + frames + "\n[" + n +
           ".0]\neffect.name=時間制御(オブジェクト)\n位置=" + row + "\n繰り返し=1\nコマ落ち=" + drop +
           "\n対象レイヤー数=1\n";
}

std::string plain_object(int no, int layer, const std::string& frames) {
    const std::string n = std::to_string(no);
    return "[" + n + "]\nlayer=" + std::to_string(layer) + "\nframe=" + frames + "\n[" + n +
           ".0]\neffect.name=標準描画\n";
}

std::string tc_project(std::int64_t source_rate, const std::string& objects) {
    return project("video.rate=" + std::to_string(source_rate) + "\nvideo.scale=1\n", objects);
}

// The rows of the output that start with prefix, in file order.
std::vector<std::string> rows_with_prefix(const RetimeResult& r, const std::string& prefix) {
    std::vector<std::string> rows;
    std::istringstream in(r.output);
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        if (line.compare(0, prefix.size(), prefix) == 0) {
            rows.push_back(line);
        }
    }
    return rows;
}

std::string first_row(const RetimeResult& r, const std::string& prefix) {
    const std::vector<std::string> rows = rows_with_prefix(r, prefix);
    return rows.empty() ? std::string("<none>") : rows[0];
}

std::string first_note(const RetimeResult& r) {
    return r.notes.empty() ? std::string("<no note>") : r.notes[0];
}

std::string refusal_or_converted(const RetimeResult& r) {
    return r.ok ? std::string("converted") : refusal_message(r);
}

// The host model for time control (AviUtl2 v2.1.12, measured with the probes in test_kit/5_抽帧探针 and 6_时间控制换算):
// the end values of 位置 are clamped to 0..100 (not each frame), p(f) = a + (b - a) (f - s) / (L - 1), the controlled
// layers show tau(f) = s + p(f) / 100 (L - 1); コマ落ち=K shows the state of the first frame of each K-frame segment
// (origin s); a 瞬間移動 track holds the value of a point until the next point, and the last point is never shown (the
// last frame shows the value of the point before it).
const char* const kStepMode = "瞬間移動";

// The original object: range s..e, end values as written, コマ落ち, and k = p / q.
struct TcOriginal {
    int64_t s = 0;
    int64_t e = 0;
    LD a = 0;
    LD b = 0;
    int64_t drop = 1;
    int64_t p = 1;
    int64_t q = 1;
};

LD clamp_percent(LD v) {
    return (std::min)((std::max)(v, static_cast<LD>(0)), static_cast<LD>(100));
}

// k tau(g(clamp(n / k, s, e))): the controlled frame, in new frames, that the original object shows at the moment of the
// new frame n.
LD tc_original_time(const TcOriginal& o, int64_t n) {
    const int64_t low = o.s * o.p;
    const int64_t fn = (std::min)((std::max)(n * o.q, low), o.e * o.p);  // clamp(n / k, s, e) = fn / p
    LD g = static_cast<LD>(fn) / static_cast<LD>(o.p);
    if (o.drop > 1) {
        g = static_cast<LD>(o.s + floor_div_i(fn - low, o.p * o.drop) * o.drop);
    }
    const LD a = clamp_percent(o.a);
    const LD b = clamp_percent(o.b);
    const LD percent = o.e == o.s ? a : a + (b - a) * (g - static_cast<LD>(o.s)) / static_cast<LD>(o.e - o.s);
    const LD tau = static_cast<LD>(o.s) + percent / 100 * static_cast<LD>(o.e - o.s);
    return tau * static_cast<LD>(o.p) / static_cast<LD>(o.q);
}

// The controlled frame that the rewritten object (its rows: frame, 位置, コマ落ち) shows at the new frame n.
LD tc_host_time(const std::map<std::string, std::string>& rows, int64_t n) {
    const std::vector<int64_t> frames = parse_ints(rows.at("frame"));
    const int64_t s2 = frames.front();
    const int64_t e2 = frames.back();
    const std::vector<std::string> position = split_commas(rows.at("位置"));
    const std::string mode = position[position.size() - 2];
    const int64_t drop = rows.count("コマ落ち") != 0 ? std::stoll(rows.at("コマ落ち")) : 1;
    const int64_t g = drop > 1 ? s2 + floor_div_i(n - s2, drop) * drop : n;
    LD percent = 0;
    if (mode == kStepMode) {
        const std::size_t count = position.size() - 2;
        std::size_t shown = 0;
        if (g >= e2) {
            shown = count - 2;  // the last point is not shown
        } else {
            for (std::size_t i = 0; i < count; ++i) {
                if (frames[i] <= g) {
                    shown = i;
                }
            }
        }
        percent = parse_number(position[shown]);
    } else {
        const LD a = clamp_percent(parse_number(position[0]));
        const LD b = clamp_percent(parse_number(position[1]));
        percent = e2 == s2 ? a : a + (b - a) * static_cast<LD>(g - s2) / static_cast<LD>(e2 - s2);
    }
    return static_cast<LD>(s2) + percent / 100 * static_cast<LD>(e2 - s2);
}

struct TcReport {
    char route = '?';        // 'A': a 直線移動(時間制御) line, 'B': 瞬間移動 steps
    std::size_t points = 0;  // frame points of the rewritten object
    std::string first_bad;   // empty when every frame matches
    bool last_frame_off = false;
};

// Compares, frame by frame, what object `obj` of the rewritten text shows with what the original shows at the same
// moment. The error allowed is the rounding of 位置 to `places` decimals. Route A is not exact on the frames whose
// original moment is outside the original range (up to one frame); route B is exact except on the last frame, which
// shows the previous step.
TcReport tc_compare(const std::string& output, int obj, const TcOriginal& o, int places) {
    TcReport report;
    const auto rows = object_rows(output, obj);
    const std::vector<int64_t> frames = parse_ints(rows.at("frame"));
    const std::vector<std::string> position = split_commas(rows.at("位置"));
    const std::string mode = position[position.size() - 2];
    report.route = mode == kTcMode ? 'A' : (mode == kStepMode ? 'B' : '?');
    report.points = frames.size();
    if (report.route == 'B' && position.size() != frames.size() + 2) {
        report.first_bad = "a 瞬間移動 row needs one value per frame point";
        return report;
    }
    if (report.route == 'A' && position.size() != 4) {
        report.first_bad = "a 直線移動(時間制御) row has two values";
        return report;
    }
    const int64_t s2 = frames.front();
    const int64_t e2 = frames.back();
    const LD tolerance =
        0.5L * pow10_neg(places) / 100 * static_cast<LD>(e2 - s2 > 1 ? e2 - s2 : 1) + static_cast<LD>(1e-9);
    for (int64_t n = s2; n <= e2 && report.first_bad.empty(); ++n) {
        // The rewritten object cannot show a frame outside its own range: when the original shows one (the new range
        // starts after k s or ends before k e, by up to half a frame) the nearest frame of the range is the best it can do.
        const LD want = (std::min)((std::max)(tc_original_time(o, n), static_cast<LD>(s2)), static_cast<LD>(e2));
        const LD got = tc_host_time(rows, n);
        const LD diff = static_cast<LD>(std::fabs(static_cast<double>(got - want)));
        const bool outside = n * o.q < o.s * o.p || n * o.q > o.e * o.p;
        bool bad = false;
        if (report.route == 'A' && outside) {
            bad = diff > 1 + tolerance;
        } else if (report.route == 'B' && n == e2 && e2 > s2) {
            report.last_frame_off = diff > tolerance;
            bad = diff > 2;
        } else {
            bad = diff > tolerance;
        }
        if (bad) {
            report.first_bad = "frame " + std::to_string(n) + " shows " + std::to_string(static_cast<double>(got)) +
                               ", the original shows " + std::to_string(static_cast<double>(want));
        }
    }
    return report;
}

TcOriginal tc_original_of(int64_t src, Rational dst, const std::string& frames, const std::string& a,
                          const std::string& b, const std::string& drop) {
    const std::vector<int64_t> range = parse_ints(frames);
    TcOriginal o;
    o.s = range.front();
    o.e = range.back();
    o.a = parse_number(a);
    o.b = parse_number(b);
    o.drop = std::stoll(drop);
    o.p = dst.num;
    o.q = dst.den * src;
    return o;
}

// Converts one time control object (object 0, with the controlled object 1) and checks the result against the model.
// want_route 'A' or 'B' (0: either). Returns the report; the retimed text is left in `output` when given.
TcReport check_time_control(const std::string& name, int64_t src, Rational dst, const std::string& frames,
                            const std::string& a, const std::string& b, const std::string& drop = "1",
                            char want_route = 0, std::string* output = nullptr,
                            std::vector<std::string>* notes = nullptr) {
    const std::string text = tc_project(src, tc_object(0, 0, frames, tc_row(a, b), drop) + plain_object(1, 1, frames));
    const RetimeResult r = retime_target(text, dst);
    check(r.ok, name + ": converts", refusal_message(r));
    TcReport report;
    if (!r.ok) {
        report.first_bad = "refused";
        return report;
    }
    const int places = (std::max)(decimal_places(a), decimal_places(b));
    report = tc_compare(r.output, 0, tc_original_of(src, dst, frames, a, b, drop), places);
    check(report.first_bad.empty(), name + ": every frame shows the original controlled time at the same moment",
          report.first_bad);
    if (want_route != 0) {
        check(report.route == want_route, std::string(name) + ": route " + want_route,
              std::string(1, report.route));
    }
    bool hint = false;
    for (const std::string& note : r.notes) {
        hint = hint || contains(note, "; last frame shows the previous step");
    }
    if (report.route == 'B') {
        check(hint == report.last_frame_off, name + ": the note says the last frame shows the previous step exactly when it does",
              hint ? "hint without a deviation" : "deviation without a hint");
    }
    if (output != nullptr) {
        *output = r.output;
    }
    if (notes != nullptr) {
        *notes = r.notes;
    }
    return report;
}

// Precise mode keeps the last value after the original end: the value shown at the new frame n is v(clamp(n / k, s, e)),
// v the piecewise-linear curve through the old keyframes. frames/values are the old keyframes and X values (mode
// 直線移動,0); want_frames/want_values the expected new frame= and X row text (without the mode). all_frames: every frame
// of the new object is checked against the original (needs the old keyframes to land on new frames); otherwise the
// keyframes only.
void check_precise_held(const std::string& name, std::int64_t src, Rational dst, const std::string& frames,
                        const std::string& values, const std::string& want_frames, const std::string& want_values,
                        bool all_frames = true) {
    const std::string text = project("video.rate=" + std::to_string(src) + "\nvideo.scale=1\n",
                                     "[0]\nlayer=0\nframe=" + frames + "\n[0.0]\neffect.name=標準描画\nX=" + values +
                                         ",直線移動,0\n");
    const RetimeResult p = free_fps::retime(text, dst, L"D:\\out\\retimed.aup2", true);
    check(p.ok, name + ": converts", refusal_message(p));
    if (!p.ok) {
        return;
    }
    const auto rows = object_rows(p.output, 0);
    check(rows.at("frame") == want_frames, name + ": frame=" + want_frames, rows.at("frame"));
    check(rows.at("X") == want_values + ",直線移動,0", name + ": X=" + want_values, rows.at("X"));
    const std::vector<int64_t> old_frames = parse_ints(frames);
    std::vector<LD> old_values;
    int places = 0;
    for (const std::string& part : split_commas(values)) {
        old_values.push_back(parse_number(part));
        places = (std::max)(places, decimal_places(part));
    }
    const std::vector<int64_t> new_frames = parse_ints(rows.at("frame"));
    const Track x = parse_track(rows.at("X"));
    check(x.values.size() == new_frames.size(), name + ": one value per frame point");
    if (x.values.size() != new_frames.size()) {
        return;
    }
    // k = p_num / q_den (the old rate is an integer): the original frame at the new frame n is n q_den / p_num.
    const int64_t p_num = dst.num;
    const int64_t q_den = dst.den * src;
    std::string first_bad;
    for (int64_t n = new_frames.front(); n <= new_frames.back() && first_bad.empty(); ++n) {
        if (!all_frames && std::find(new_frames.begin(), new_frames.end(), n) == new_frames.end()) {
            continue;
        }
        const int64_t fn = (std::min)((std::max)(n * q_den, old_frames.front() * p_num), old_frames.back() * p_num);
        const LD f = static_cast<LD>(fn) / static_cast<LD>(p_num);  // clamp(n / k, s, e)
        LD expect = old_values.back();
        for (std::size_t i = 0; i + 1 < old_frames.size(); ++i) {
            if (f <= static_cast<LD>(old_frames[i + 1])) {
                expect = old_values[i] + (old_values[i + 1] - old_values[i]) * (f - static_cast<LD>(old_frames[i])) /
                                             static_cast<LD>(old_frames[i + 1] - old_frames[i]);
                break;
            }
        }
        const LD got = host_line_value(new_frames, x.values, n);
        if (std::fabs(static_cast<double>(got - expect)) > 0.5 * static_cast<double>(pow10_neg(places)) + 1e-9) {
            first_bad = "frame " + std::to_string(n) + " shows " + std::to_string(static_cast<double>(got)) +
                        ", the original shows " + std::to_string(static_cast<double>(expect));
        }
    }
    check(first_bad.empty(),
          name + (all_frames ? ": every frame" : ": every keyframe") + " shows the original value at the same moment",
          first_bad);
}

void test_precise_hold() {
    // Linear objects whose new range is longer than the original in time: the value stops at the end value.
    check_precise_held("precise hold 30->60", 30, rate(60), "0,29", "0.00,100.00", "0,58,59", "0.00,100.00,100.00");
    check_precise_held("precise hold 66->132", 66, rate(132), "0,329", "0.00,100.00", "0,658,659",
                       "0.00,100.00,100.00");
    check_precise_held("precise hold 30->60000/1001", 30, rate(60000, 1001), "0,149", "-200.00,200.00",
                       "0,297,298,299", "-200.00,199.06,200.00,200.00");
    check_precise_held("precise hold 30->66 (e k = 63.8 is not an integer)", 30, rate(66), "0,29", "0.00,100.00",
                       "0,63,64,65", "0.00,98.75,100.00,100.00");
    check_precise_held("precise hold 30->66 from frame 3", 30, rate(66), "3,12", "0.00,90.00", "7,26,27,28",
                       "1.82,88.18,90.00,90.00");
    // The new object starts before the original does (s k = 2.2, s' = 2): the first value is held up to s k.
    check_precise_held("precise hold start 30->66 (s' / k < s)", 30, rate(66), "1,10", "0.00,90.00", "2,3,22,23",
                       "0.00,3.64,90.00,90.00");
    // k < 1: nothing is added when the new object ends inside the original; a point is added when it starts early.
    check_precise_held("precise hold 60->30 (no clamp, unchanged)", 60, rate(30), "0,59", "0.00,100.00", "0,29",
                       "0.00,98.31");
    check_precise_held("precise hold start 30->10 (k < 1, s' / k < s)", 30, rate(10), "7,29", "0.00,100.00", "2,3,9",
                       "0.00,9.09,90.91");
    // Several keyframes: the extra points follow the last ones; the kink lands on a new frame (10 x 2.2 = 22).
    check_precise_held("precise hold 3 points 30->60", 30, rate(60), "0,10,29", "0.00,50.00,100.00", "0,20,58,59",
                       "0.00,50.00,100.00,100.00");
    check_precise_held("precise hold 3 points 30->66", 30, rate(66), "0,10,29", "0.00,50.00,100.00", "0,22,63,64,65",
                       "0.00,50.00,99.04,100.00,100.00");
    // A decreasing line holds its last (lowest) value.
    check_precise_held("precise hold decreasing 30->60", 30, rate(60), "0,29", "100.00,-100.00", "0,58,59",
                       "100.00,-100.00,-100.00");

    {
        // Constant tracks repeat their value at every keyframe; the frame= row is written once with all the points.
        const std::string text = project(scene30(), "[0]\nlayer=0\nframe=0,29\n[0.0]\neffect.name=標準描画\n"
                                                    "X=0.00,100.00,直線移動,0\nY=5.00,5.00,補間移動,0\n");
        const RetimeResult p = free_fps::retime(text, rate(66), L"D:\\out\\retimed.aup2", true);
        check(p.ok && first_row(p, "frame=") == "frame=0,63,64,65" &&
                  first_row(p, "Y=") == "Y=5.00,5.00,5.00,5.00,補間移動,0",
              "precise hold: a constant track repeats its value at each of the four keyframes", refusal_message(p));
        int frame_changes = 0;
        for (const auto& change : p.changes) {
            frame_changes += change.old_text && change.old_text->compare(0, 6, "frame=") == 0 ? 1 : 0;
        }
        check(frame_changes == 1, "precise hold: the frame= row is changed once", std::to_string(frame_changes));
        check(p.precise && p.precise->applied == 1 && p.precise->tracks.size() == 2 &&
                  p.precise->tracks[0].end_extrapolation == "0.000000",
              "precise hold: the report counts the object once and the end shift is 0");
        const RetimeResult plain = retime_target(text, rate(66));
        check(plain.ok && first_row(plain, "frame=") == "frame=0,65" &&
                  first_row(plain, "X=") == "X=0.00,100.00,直線移動,0",
              "precise off: frame= and X keep the plain rescaled form", refusal_message(plain));
    }
    {
        // An object that has a time control effect is left alone: its 2-point frame= and its other trackbars stay as
        // they were (the time control rewrites its own rows); it is neither applied nor listed as skipped.
        const std::string text = tc_project(
            30, "[0]\nlayer=0\nframe=0,29\n[0.0]\neffect.name=時間制御(オブジェクト)\n位置=" + tc_row("0.000", "100.000") +
                    "\n繰り返し=1\nコマ落ち=1\n対象レイヤー数=1\n[0.1]\neffect.name=標準描画\nX=0.00,100.00,直線移動,0\n" +
                    plain_object(1, 1, "0,29"));
        const RetimeResult p = free_fps::retime(text, rate(60), L"D:\\out\\retimed.aup2", true);
        check(p.ok && first_row(p, "frame=") == "frame=0,59" && first_row(p, "X=") == "X=0.00,100.00,直線移動,0" &&
                  p.precise && p.precise->applied == 0 && p.precise->skipped.empty() && p.precise->tracks.empty(),
              "precise hold: an object with a time control effect keeps its 2-point frame= and its X row, not listed",
              refusal_message(p));
    }
    {
        // A 再生範囲 row is left alone; the object is held and extended as usual.
        const std::string text = project(scene30(), "[0]\nlayer=0\nframe=1,10\n[0.0]\neffect.name=標準描画\n"
                                                    "X=0.00,90.00,直線移動,0\nR=0,10,再生範囲,1\n");
        const RetimeResult p = free_fps::retime(text, rate(66), L"D:\\out\\retimed.aup2", true);
        check(p.ok && first_row(p, "frame=") == "frame=2,3,22,23" && first_row(p, "R=") == "R=0,10,再生範囲,1",
              "precise hold: a 再生範囲 row is not given extra values", refusal_message(p));
    }
    {
        // Shortened to one frame: the two equal frame points stay, with the value at that moment.
        const std::string text = project(scene30(), "[0]\nlayer=0\nframe=2,3\n[0.0]\neffect.name=標準描画\n"
                                                    "X=0.00,100.00,直線移動,0\n");
        const RetimeResult p = free_fps::retime(text, rate(12), L"D:\\out\\retimed.aup2", true);
        check(p.ok && first_row(p, "frame=") == "frame=1,1" && first_row(p, "X=") == "X=50.00,50.00,直線移動,0",
              "precise hold: a one-frame object keeps frame=1,1 and two values", refusal_message(p));
    }
}


void test_time_control_positions() {
    struct Case {
        std::int64_t source;
        Rational target;
        const char* frames;
        const char* a;
        const char* b;
        const char* frames_out;  // route A: the frame= text; route B (a_out == nullptr): the first and the last frame
        const char* a_out;
        const char* b_out;
    };
    // ARZ-style 12-frame blocks (0 -> 100 forward, 100 -> 0 reversed) on the first and the second block position.
    // Route A (a_out given): the plain line stays in 0..100 at both ends. Route B: the line would leave 0..100, so the
    // 位置 row becomes steps (the host clamps only the end values, not each frame).
    const Case cases[] = {
        // 66 -> 60 (k = 10/11): the frame lists are 0,10 and 11,21. Only the reversed second block needs steps.
        {66, rate(60), "0,11", "0.000", "100.000", "0,10", "0.000", "100.000"},
        {66, rate(60), "12,23", "0.000", "100.000", "11,21", "0.000", "100.000"},
        {66, rate(60), "0,11", "100.000", "0.000", "0,10", "100.000", "0.000"},
        {66, rate(60), "12,23", "100.000", "0.000", "11,21", nullptr, nullptr},
        // 30 -> 60 (k = 2): a reversed block would end below 0.
        {30, rate(60), "0,11", "0.000", "100.000", "0,23", "0.000", "100.000"},
        {30, rate(60), "12,23", "0.000", "100.000", "24,47", "0.000", "100.000"},
        {30, rate(60), "0,11", "100.000", "0.000", "0,23", nullptr, nullptr},
        {30, rate(60), "12,23", "100.000", "0.000", "24,47", nullptr, nullptr},
        // 30 -> 66 (k = 11/5).
        {30, rate(66), "0,11", "0.000", "100.000", "0,25", "0.000", "100.000"},
        {30, rate(66), "0,11", "100.000", "0.000", "0,25", nullptr, nullptr},
        {30, rate(66), "12,23", "100.000", "0.000", "26,52", nullptr, nullptr},
        // The decimals of the row are kept: none here, rounded half away from zero (the steps too).
        {30, rate(66), "0,11", "100", "0", "0,25", nullptr, nullptr},
        // 66 -> 132 (k = 2): forward is exact, reversed needs steps.
        {66, rate(132), "0,11", "0.000", "100.000", "0,23", "0.000", "100.000"},
        {66, rate(132), "0,11", "100.000", "0.000", "0,23", nullptr, nullptr},
        // A middle range stays inside 0..100 in both directions.
        {30, rate(60), "0,11", "20.000", "80.000", "0,23", "19.130", "79.130"},
        {30, rate(60), "0,11", "80.000", "20.000", "0,23", "76.522", "16.522"},
        // The host clamps the end values: -50 -> 100 acts as 0 -> 100, 0 -> 150 as 0 -> 100.
        {30, rate(60), "0,11", "-50.000", "100.000", "0,23", "0.000", "100.000"},
        {30, rate(60), "0,11", "0.000", "150.000", "0,23", "0.000", "100.000"},
        // A single new frame: the host shows s' whatever 位置 is; the original line's value at that moment is written.
        {30, rate(12), "2,3", "0.000", "100.000", "1,1", "50.000", "50.000"},
    };
    for (const Case& c : cases) {
        const bool steps = c.a_out == nullptr;
        const std::string name = std::string("time control ") + std::to_string(c.source) + "->" +
                                 std::to_string(c.target.num) + " " + c.a + "," + c.b + " at " + c.frames;
        std::string output;
        std::vector<std::string> notes;
        check_time_control(name, c.source, c.target, c.frames, c.a, c.b, "1", steps ? 'B' : 'A', &output, &notes);
        if (output.empty()) {
            continue;
        }
        const auto rows = object_rows(output, 0);
        const std::string text = tc_project(c.source, tc_object(0, 0, c.frames, tc_row(c.a, c.b)) +
                                                          plain_object(1, 1, c.frames));
        const std::string note_head = "time control at line " +
                                      std::to_string(line_of_first(text, std::string("frame=") + c.frames)) + ": 位置 " +
                                      c.a + "," + c.b + " -> ";
        if (steps) {
            const std::vector<int64_t> got = parse_ints(rows.at("frame"));
            const std::vector<int64_t> want = parse_ints(c.frames_out);
            check(got.front() == want.front() && got.back() == want.back() && got.size() > 2,
                  name + ": frame= runs from " + c.frames_out + " with a point per step", rows.at("frame"));
            const std::vector<std::string> position = split_commas(rows.at("位置"));
            check(position[position.size() - 2] == kStepMode && position.back() == "0",
                  name + ": 位置 is a 瞬間移動 row with flag 0");
            check(notes.size() == 1 && notes[0].compare(0, note_head.size(), note_head) == 0 &&
                      contains(notes[0], " steps (瞬間移動)"),
                  name + ": note", notes.empty() ? "" : notes[0]);
            continue;
        }
        check(rows.at("frame") == c.frames_out, name + ": frame= " + c.frames_out, rows.at("frame"));
        const std::string expected = std::string(c.a_out) + "," + c.b_out + "," + kTcMode + ",0";
        check(rows.at("位置") == expected, name + ": 位置=" + expected, rows.at("位置"));
        const bool unchanged = std::string(c.a) == c.a_out && std::string(c.b) == c.b_out;
        if (unchanged) {
            check(notes.empty(), name + ": unchanged 位置 gets no note", notes.empty() ? "" : notes[0]);
        } else {
            check(notes.size() == 1 && notes[0] == note_head + c.a_out + "," + c.b_out, name + ": note",
                  notes.empty() ? "" : notes[0]);
        }
    }
}

void test_time_control_two_blocks() {
    // Two reversed blocks on one layer that touch (11 | 12): both become steps, the notes follow the file order, and the
    // frame lists still touch (first and last frame are those of the plain rescaling).
    const std::string text = tc_project(30, tc_object(0, 0, "0,11", tc_row("100.000", "0.000")) +
                                                tc_object(1, 0, "12,23", tc_row("100.000", "0.000")) +
                                                plain_object(2, 1, "0,23"));
    const RetimeResult r = retime_target(text, rate(66));
    check(r.ok, "time control two blocks 30->66: converts", refusal_message(r));
    if (!r.ok) {
        return;
    }
    const TcReport first = tc_compare(r.output, 0, tc_original_of(30, rate(66), "0,11", "100.000", "0.000", "1"), 3);
    const TcReport second = tc_compare(r.output, 1, tc_original_of(30, rate(66), "12,23", "100.000", "0.000", "1"), 3);
    check(first.route == 'B' && first.first_bad.empty() && second.route == 'B' && second.first_bad.empty(),
          "time control two blocks 30->66: both blocks show the original controlled time on every frame",
          first.first_bad + second.first_bad);
    const std::vector<int64_t> f0 = parse_ints(object_rows(r.output, 0).at("frame"));
    const std::vector<int64_t> f1 = parse_ints(object_rows(r.output, 1).at("frame"));
    check(f0.front() == 0 && f0.back() == 25 && f1.front() == 26 && f1.back() == 52,
          "time control two blocks: the frame lists still touch (0..25, 26..52)");
    const std::string note0 = "time control at line " + std::to_string(line_of_first(text, "frame=0,11")) +
                              ": 位置 100.000,0.000 -> " + std::to_string(f0.size()) + " steps (瞬間移動)";
    const std::string note1 = "time control at line " + std::to_string(line_of_first(text, "frame=12,23")) +
                              ": 位置 100.000,0.000 -> " + std::to_string(f1.size()) + " steps (瞬間移動)";
    check(r.notes.size() == 2 && r.notes[0].compare(0, note0.size(), note0) == 0 &&
              r.notes[1].compare(0, note1.size(), note1) == 0,
          "time control two blocks: one note per object, in file order", r.notes.empty() ? "" : r.notes[0]);
    // The sidecar carries the notes and the rewritten rows.
    const std::string json = free_fps::sidecar_json(r, L"D:\\in.aup2", L"D:\\out\\retimed.aup2");
    check(contains(json, "time control at line") && contains(json, ",瞬間移動,0\""),
          "time control two blocks: sidecar lists the notes and the changed rows");
}

void test_easing_modes() {
    const std::string easing = "イージング（通常）@イージング";
    const std::string easing_move = "イージング（補間移動）@イージング";
    auto object = [](const std::string& frames, const std::string& row) {
        return "[0]\nlayer=0\nframe=" + frames + "\n[0.0]\neffect.name=標準描画\n" + row + "\n";
    };
    {
        // The easing mode and its '|' payload are kept as written; the frames are rescaled (multi-point as usual).
        const std::string text = project(scene30(), object("0,10,29", "X=0.00,50.00,100.00," + easing + ",0|27"));
        const RetimeResult r = retime_target(text, rate(60));
        check(r.ok && first_row(r, "frame=") == "frame=0,20,59" &&
                  first_row(r, "X=") == "X=0.00,50.00,100.00," + easing + ",0|27",
              "easing: mode and | payload kept, frames rescaled", refusal_or_converted(r));
        const RetimeResult plain = retime_target(project(scene30(), object("0,29", "X=0.00,100.00," + easing + ",0")), rate(66));
        check(plain.ok && first_row(plain, "X=") == "X=0.00,100.00," + easing + ",0", "easing: no payload is kept too",
              refusal_or_converted(plain));
    }
    {
        const std::string text = project(scene30(), object("0,29", "X=0.00,100.00," + easing_move + ",0"));
        expect_refused("イージング（補間移動）@イージング is not on the whitelist", text, rate(60), line_of_first(text, "X="),
                       "trackbar mode '" + easing_move + "' is not on the whitelist");
    }
    {
        const std::string text = project(scene30(), object("0,29", "X=0.00,100.00,補間移動,0|27"));
        expect_refused("a | payload on another mode is still refused", text, rate(60), line_of_first(text, "X="),
                       "trackbar '|' payload '27' is not on the whitelist");
    }
    {
        // The value count must match the frame points as for every other mode.
        const std::string text = project(scene30(), object("0,29", "X=0.00,50.00,100.00," + easing + ",0|27"));
        expect_refused("easing: value count must equal the frame points", text, rate(60), line_of_first(text, "X="),
                       "trackbar has 3 values");
    }
    // Precise mode: a payload skips the object (checked first, also for constant values); changing values without a
    // payload skip it with the usual mode text; constant values without a payload are re-sampled.
    {
        const std::string text = project(scene30(), object("0,29", "X=0.00,100.00," + easing + ",0|27"));
        const RetimeResult p = free_fps::retime(text, rate(60), L"D:\\out\\retimed.aup2", true);
        check(p.ok && p.precise && p.precise->applied == 0 && p.precise->skipped.size() == 1 &&
                  p.precise->skipped[0].line == line_of_first(text, "frame=0,29") &&
                  p.precise->skipped[0].reason == "trackbar at line " + std::to_string(line_of_first(text, "X=")) +
                                                      " has a '|' payload",
              "easing precise: a | payload skips the object", p.precise && !p.precise->skipped.empty()
                                                                  ? p.precise->skipped[0].reason : refusal_message(p));
        check(first_row(p, "X=") == "X=0.00,100.00," + easing + ",0|27", "easing precise: the skipped row is unchanged");
    }
    {
        const std::string text = project(scene30(), object("0,29", "X=5.00,5.00," + easing + ",0|27"));
        const RetimeResult p = free_fps::retime(text, rate(60), L"D:\\out\\retimed.aup2", true);
        check(p.ok && p.precise && p.precise->skipped.size() == 1 &&
                  contains(p.precise->skipped[0].reason, "has a '|' payload"),
              "easing precise: constant values with a payload are skipped as well", refusal_message(p));
    }
    {
        const std::string text = project(scene30(), object("0,29", "X=0.00,100.00," + easing + ",0"));
        const RetimeResult p = free_fps::retime(text, rate(60), L"D:\\out\\retimed.aup2", true);
        check(p.ok && p.precise && p.precise->applied == 0 && p.precise->skipped.size() == 1 &&
                  p.precise->skipped[0].reason ==
                      "trackbar at line " + std::to_string(line_of_first(text, "X=")) + " uses mode " + easing +
                          " with parameter 0 and changing values; only 直線移動,0 or constant values can be resampled exactly",
              "easing precise: changing values without a payload are skipped with the mode text",
              p.precise && !p.precise->skipped.empty() ? p.precise->skipped[0].reason : refusal_message(p));
    }
    {
        const std::string text = project(scene30(), object("0,29", "X=5.00,5.00," + easing + ",0"));
        const RetimeResult p = free_fps::retime(text, rate(60), L"D:\\out\\retimed.aup2", true);
        check(p.ok && p.precise && p.precise->applied == 1 && p.precise->skipped.empty() &&
                  first_row(p, "X=") == "X=5.00,5.00,5.00," + easing + ",0",
              "easing precise: constant values without a payload are re-sampled (repeated at each keyframe)",
              refusal_message(p));
    }
}

void test_time_control_two_effects_one_object() {
    // Two time control effects on one object (both straight lines): one note each, with the same object line, in the
    // order of their 位置 rows.
    const std::string effects = "[0]\nlayer=0\nframe=0,59\n[0.0]\neffect.name=時間制御(オブジェクト)\n位置=" +
                                tc_row("0.000", "100.000") + "\n繰り返し=1\nコマ落ち=2\n対象レイヤー数=1\n"
                                "[0.1]\neffect.name=時間制御(オブジェクト)\n位置=" + tc_row("80.000", "20.000") +
                                "\n繰り返し=1\nコマ落ち=4\n対象レイヤー数=1\n" + plain_object(1, 1, "0,59");
    const std::string text = tc_project(30, effects);
    const RetimeResult r = retime_target(text, rate(60));
    const std::size_t object_line = line_of_first(text, "frame=0,59");
    check(r.ok && r.notes.size() == 2 &&
              r.notes[0] == "time control at line " + std::to_string(object_line) +
                                ": 位置 0.000,100.000 -> 0.000,100.000; コマ落ち 2 -> 4" &&
              r.notes[1] == "time control at line " + std::to_string(object_line) +
                                ": 位置 80.000,20.000 -> 79.328,19.328; コマ落ち 4 -> 8",
          "time control: two effects on one object get one note each", r.ok ? first_note(r) : refusal_message(r));
    const std::vector<std::string> drops = rows_with_prefix(r, "コマ落ち=");
    check(drops.size() == 2 && drops[0] == "コマ落ち=4" && drops[1] == "コマ落ち=8",
          "time control: both コマ落ち rows are scaled");
    // The changes list the time control rows last, in line order (位置 and コマ落ち mixed).
    std::size_t last_frame_change = 0;
    std::size_t first_tc_change = 0;
    for (std::size_t i = 0; i < r.changes.size(); ++i) {
        const auto& old_text = r.changes[i].old_text;
        if (old_text && old_text->compare(0, 6, "frame=") == 0) {
            last_frame_change = i;
        }
        if (old_text && (old_text->compare(0, 7, "位置=") == 0 || old_text->compare(0, 13, "コマ落ち=") == 0) &&
            first_tc_change == 0) {
            first_tc_change = i;
        }
    }
    bool ascending = true;
    for (std::size_t i = first_tc_change + 1; i < r.changes.size(); ++i) {
        ascending = ascending && r.changes[i].line > r.changes[i - 1].line;
    }
    check(first_tc_change > last_frame_change && ascending && r.changes.size() - first_tc_change == 3,
          "time control: changes come after the frame= rows, in line order (位置 0.000,100.000 is unchanged)");
}

void test_time_control_frame_drop() {
    struct Case {
        std::int64_t source;
        Rational target;
        const char* drop;
        const char* drop_out;
        char route;
    };
    const Case cases[] = {
        {30, rate(60), "2", "4", 'A'},      // k = 2
        {30, rate(90), "2", "6", 'A'},      // k = 3
        {30, rate(60), "3", "6", 'A'},
        {66, rate(132), "3", "6", 'A'},
        {30, rate(15), "2", "1", 'A'},      // k = 1/2: two input frames per segment become one output frame
        {30, rate(60), "1", "1", 'A'},      // no drop stays
        {30, rate(60), "01", "01", 'A'},    // numerically 1: left as it is
        {30, rate(66), "5", "11", 'A'},     // k = 11/5
        // K x k is not an integer: the segments no longer have a whole number of frames, so the steps are written and
        // コマ落ち becomes 1 (before, these were refused).
        {30, rate(66), "2", "1", 'B'},      // K x k = 22/5
        {66, rate(60), "3", "1", 'B'},      // K x k = 30/11
        {30, rate(60000, 1001), "2", "1", 'B'},  // K x k = 4000/1001
    };
    for (const Case& c : cases) {
        const std::string name = std::string("コマ落ち=") + c.drop + " at " + std::to_string(c.source) + "->" +
                                 std::to_string(c.target.num) + (c.target.den != 1 ? "/" + std::to_string(c.target.den) : "");
        const std::string text = tc_project(c.source, tc_object(0, 0, "0,59", tc_row("0.000", "100.000"), c.drop) +
                                                          plain_object(1, 1, "0,59"));
        const RetimeResult r = retime_target(text, c.target);
        check(r.ok, name + ": converts", refusal_message(r));
        if (!r.ok) {
            continue;
        }
        check(first_row(r, "コマ落ち=") == std::string("コマ落ち=") + c.drop_out, name + " -> " + c.drop_out,
              first_row(r, "コマ落ち="));
        const bool rewritten = std::string(c.drop) != c.drop_out;
        bool noted = false;
        for (const std::string& note : r.notes) {
            noted = noted || contains(note, std::string("; コマ落ち ") + c.drop + " -> " + c.drop_out);
        }
        check(noted == rewritten, name + ": the note names コマ落ち only when it was rewritten", first_note(r));
        check(first_row(r, "繰り返し=") == "繰り返し=1" && first_row(r, "対象レイヤー数=") == "対象レイヤー数=1",
              name + ": 繰り返し and 対象レイヤー数 are untouched");
        check_time_control(name, c.source, c.target, "0,59", "0.000", "100.000", c.drop, c.route);
    }
    // The full note of a rewritten pair: steps (100 -> 0 at 30 -> 60 leaves 0..100), K x k = 4 but the line leaves the range.
    const std::string text = tc_project(30, tc_object(0, 0, "0,59", tc_row("100.000", "0.000"), "2") +
                                                plain_object(1, 1, "0,59"));
    const RetimeResult r = retime_target(text, rate(60));
    check(r.ok && r.notes.size() == 1 &&
              r.notes[0] == "time control at line " + std::to_string(line_of_first(text, "frame=0,59")) +
                                ": 位置 100.000,0.000 -> 31 steps (瞬間移動); コマ落ち 2 -> 1",
          "time control note: steps and コマ落ち in one note", first_note(r));
    check_time_control("コマ落ち=2 100->0 at 30->60", 30, rate(60), "0,59", "100.000", "0.000", "2", 'B');
    check_time_control("コマ落ち=2 20->80 at 30->60", 30, rate(60), "0,59", "20.000", "80.000", "2", 'A');
}

void test_time_control_steps() {
    // 30 -> 66 (k = 11/5), コマ落ち=2, 0 -> 100: the original shows tau of frames 0, 2, 4, ... and a new segment starts at
    // the new frame ceil((s + j K) k) = ceil(22 j / 5): 0, 5, 9, 14, 18, 22. The last point (25) is not a step.
    {
        std::string output;
        std::vector<std::string> notes;
        check_time_control("steps 30->66 K=2", 30, rate(66), "0,11", "0.000", "100.000", "2", 'B', &output, &notes);
        const auto rows = output.empty() ? std::map<std::string, std::string>() : object_rows(output, 0);
        std::string boundaries;
        for (int j = 0; (22 * j + 4) / 5 <= 25; ++j) {
            boundaries += (j > 0 ? "," : "") + std::to_string((22 * j + 4) / 5);
        }
        check(!rows.empty() && rows.at("frame") == boundaries + ",25",
              "steps 30->66 K=2: a point at each ceil((s + j K) k), then the last frame " + boundaries + ",25",
              rows.empty() ? "" : rows.at("frame"));
        check(!rows.empty() && rows.at("位置") == "0.000,17.600,35.200,52.800,70.400,88.000,88.000,瞬間移動,0" &&
                  rows.at("コマ落ち") == "1",
              "steps 30->66 K=2: 位置 holds p' of each segment, コマ落ち=1", rows.empty() ? "" : rows.at("位置"));
    }
    // 66 -> 132, 100 -> 0: one point per frame, p' = 100 (22 - n) / 23, then 0 on the last two frames.
    {
        std::string output;
        check_time_control("steps 66->132 reversed", 66, rate(132), "0,11", "100.000", "0.000", "1", 'B', &output);
        const auto rows = output.empty() ? std::map<std::string, std::string>() : object_rows(output, 0);
        check(!rows.empty() && rows.at("frame") == "0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23" &&
                  rows.at("位置") == "95.652,91.304,86.957,82.609,78.261,73.913,69.565,65.217,60.870,56.522,52.174,"
                                     "47.826,43.478,39.130,34.783,30.435,26.087,21.739,17.391,13.043,8.696,4.348,"
                                     "0.000,0.000,瞬間移動,0",
              "steps 66->132 reversed: one value per frame, 0.000 on the last two frames", rows.empty() ? "" : rows.at("位置"));
    }
    // 66 -> 60, second reversed block: the new last frame shows tau of the original last frame, the host cannot show a
    // last point that differs, so the note says so.
    {
        std::string output;
        std::vector<std::string> notes;
        check_time_control("steps 66->60 reversed block", 66, rate(60), "12,23", "100.000", "0.000", "1", 'B', &output,
                           &notes);
        const std::string head = "time control at line 16: 位置 100.000,0.000 -> 11 steps (瞬間移動)";
        check(notes.size() == 1 && notes[0] == head + "; last frame shows the previous step",
              "steps 66->60 reversed block: note with the last-frame hint", notes.empty() ? "" : notes[0]);
    }
    // A segment origin s k that is not an integer: K = 5 at 30 -> 66 (K x k = 11) from frame 1 (s k = 2.2) needs steps; from
    // frame 0 it is a plain line with コマ落ち=11.
    {
        std::string output;
        check_time_control("origin 30->66 K=5 from frame 1", 30, rate(66), "1,20", "0.000", "100.000", "5", 'B', &output);
        check(!output.empty() && object_rows(output, 0).at("コマ落ち") == "1", "origin 30->66 K=5 from frame 1: コマ落ち=1");
        check_time_control("origin 30->66 K=5 from frame 0", 30, rate(66), "0,59", "0.000", "100.000", "5", 'A', &output);
        check(!output.empty() && object_rows(output, 0).at("コマ落ち") == "11", "origin 30->66 K=5 from frame 0: コマ落ち=11");
    }
    // The new range starts after k s (3 x 2.2 = 6.6, s' = 7): p' would be -0.4 / 25 percent on the first frame, steps clamp it.
    {
        std::string output;
        check_time_control("clamped start 30->66 K=2 from frame 3", 30, rate(66), "3,14", "0.000", "100.000", "2", 'B',
                           &output);
        const auto rows = output.empty() ? std::map<std::string, std::string>() : object_rows(output, 0);
        check(!rows.empty() && rows.at("位置").compare(0, 6, "0.000,") == 0,
              "clamped start 30->66 K=2 from frame 3: the first step is 0.000, not negative",
              rows.empty() ? "" : rows.at("位置"));
    }
    // The flag of the 位置 row is written as 0 on steps and kept on a line.
    {
        const std::string text = tc_project(30, tc_object(0, 0, "0,11", tc_row("100.000", "0.000", "1")) +
                                                    plain_object(1, 1, "0,11"));
        const RetimeResult r = retime_target(text, rate(60));
        const std::vector<std::string> position = split_commas(first_row(r, "位置=").substr(std::string("位置=").size()));
        check(r.ok && position.size() > 4 && position[position.size() - 2] == kStepMode && position.back() == "0",
              "steps: flag 1 is written as 0 on a 瞬間移動 row", first_row(r, "位置="));
        const std::string forward = tc_project(30, tc_object(0, 0, "0,11", tc_row("0.000", "100.000", "1")) +
                                                       plain_object(1, 1, "0,11"));
        check(first_row(retime_target(forward, rate(60)), "位置=") == std::string("位置=0.000,100.000,") + kTcMode + ",1",
              "steps: flag 1 is kept on a straight line");
    }
    // Without a コマ落ち row: K = 1 and nothing is written for it.
    {
        const std::string text =
            tc_project(30, "[0]\nlayer=0\nframe=0,11\n[0.0]\neffect.name=時間制御(オブジェクト)\n位置=" +
                               tc_row("100.000", "0.000") + "\n繰り返し=1\n対象レイヤー数=1\n" + plain_object(1, 1, "0,11"));
        const RetimeResult r = retime_target(text, rate(60));
        check(r.ok && rows_with_prefix(r, "コマ落ち=").empty() && first_row(r, "位置=").find("瞬間移動") != std::string::npos,
              "steps: a time control without a コマ落ち row converts and gets none", refusal_message(r));
    }
    // A single new frame: the first frame of the only segment is shown whatever K is; K x k is written when it is an integer.
    {
        const std::string text = tc_project(30, tc_object(0, 0, "2,3", tc_row("0.000", "100.000"), "4") + plain_object(1, 1, "2,3"));
        const RetimeResult r = retime_target(text, rate(15));
        check(r.ok && first_row(r, "frame=") == "frame=1,1" && first_row(r, "コマ落ち=") == "コマ落ち=2" &&
                  contains(first_note(r), "; コマ落ち 4 -> 2"),
              "one new frame: コマ落ち=4 becomes K x k = 2", refusal_or_converted(r));
        const std::string text2 = tc_project(30, tc_object(0, 0, "2,3", tc_row("0.000", "100.000"), "2") + plain_object(1, 1, "2,3"));
        const RetimeResult r2 = retime_target(text2, rate(12));
        check(r2.ok && first_row(r2, "コマ落ち=") == "コマ落ち=1" && contains(first_note(r2), "; コマ落ち 2 -> 1") &&
                  first_row(r2, "位置=") == std::string("位置=50.000,50.000,") + kTcMode + ",0",
              "one new frame: コマ落ち=2 with K x k = 4/5 becomes 1, 位置 is p at that moment", refusal_or_converted(r2));
    }
    // The 4096 point limit (first and last point included): the longest object that converts has exactly 4096 points,
    // the next frame counts refuse. 30 -> 66, コマ落ち=2 gives one step every 4.4 frames.
    {
        std::vector<std::pair<int, std::size_t>> results;  // (last frame, points), points 0: refused
        std::string refusal;
        for (int last = 8180; last <= 8200; ++last) {
            const std::string frames = "0," + std::to_string(last);
            const std::string text = tc_project(30, tc_object(0, 0, frames, tc_row("0.000", "100.000"), "2") +
                                                        plain_object(1, 1, frames));
            const RetimeResult r = retime_target(text, rate(66));
            if (r.ok) {
                results.emplace_back(last, parse_ints(object_rows(r.output, 0).at("frame")).size());
            } else {
                results.emplace_back(last, 0);
                if (refusal.empty()) {
                    refusal = refusal_message(r);
                }
            }
        }
        std::size_t largest_ok = 0;
        bool monotone = true;
        bool refused_seen = false;
        for (const auto& entry : results) {
            if (entry.second == 0) {
                refused_seen = true;
            } else {
                monotone = monotone && !refused_seen;
                largest_ok = (std::max)(largest_ok, entry.second);
            }
        }
        check(refused_seen && monotone && largest_ok == 4096,
              "4096 steps: converts up to exactly 4096 points, refuses beyond", "largest " + std::to_string(largest_ok));
        const std::string text = tc_project(30, tc_object(0, 0, "0,29999", tc_row("100.000", "0.000")) +
                                                    plain_object(1, 1, "0,29999"));
        const RetimeResult r = retime_target(text, rate(60));
        check(!r.ok && r.refusal_line == 0 &&
                  refusal_message(r) == "REFUSED: time control at line " + std::to_string(line_of_first(text, "位置=")) +
                                            " needs more than 4096 steps",
              "4096 steps: a 30000-frame reversed block is refused with the 位置 line", refusal_or_converted(r));
        check(refusal.find("needs more than 4096 steps") != std::string::npos,
              "4096 steps: the refusal text names the limit", refusal);
    }
}

void test_time_control_refusals() {
    const auto with_drop = [](const std::string& value, Rational target, std::int64_t source) {
        const std::string text = tc_project(
            source, tc_object(0, 0, "0,59", tc_row("0.000", "100.000"), value) + plain_object(1, 1, "0,59"));
        return std::make_pair(text, retime_target(text, target));
    };
    {
        // K x k does not have to be an integer any more (these three were refused before): steps are written instead.
        for (const auto& c : {std::make_pair(std::string("2"), std::make_pair(std::int64_t(30), rate(66))),
                              std::make_pair(std::string("3"), std::make_pair(std::int64_t(66), rate(60))),
                              std::make_pair(std::string("2"), std::make_pair(std::int64_t(30), rate(60000, 1001)))}) {
            const auto result = with_drop(c.first, c.second.second, c.second.first);
            const RetimeResult& r = result.second;
            check(r.ok && first_row(r, "コマ落ち=") == "コマ落ち=1" && contains(first_row(r, "位置="), "瞬間移動"),
                  "コマ落ち=" + c.first + " at " + std::to_string(c.second.first) + "->" +
                      std::to_string(c.second.second.num) + " is converted to steps with コマ落ち=1",
                  refusal_or_converted(r));
        }
    }
    for (const char* bad : {"0", "1.5", "-2", "abc", "", "0,10,直線移動,0", "2,4,直線移動,0"}) {
        const auto result = with_drop(bad, rate(60), 30);
        const RetimeResult& r = result.second;
        check(!r.ok && r.refusal_line == line_of_first(result.first, "コマ落ち=") &&
                  r.refusal_reason == std::string("コマ落ち=") + bad + " (frame drop) must be a positive integer",
              std::string("コマ落ち=") + bad + " is refused as not a positive integer", refusal_or_converted(r));
    }
    const std::string kTcRefusal = "time control 位置 must be a two-point 直線移動(時間制御) track";
    {
        const std::string text =
            tc_project(30, tc_object(0, 0, "0,30,59", tc_row("0.000", "100.000")) + plain_object(1, 1, "0,59"));
        expect_refused("time control on a three-point object is refused", text, rate(60), line_of_first(text, "位置="),
                       kTcRefusal);
    }
    {
        const std::string text = tc_project(
            30, tc_object(0, 0, "0,59", std::string("0.000,50.000,100.000,") + kTcMode + ",0") + plain_object(1, 1, "0,59"));
        expect_refused("time control with three positions is refused", text, rate(60), line_of_first(text, "位置="),
                       kTcRefusal);
    }
    {
        // A | payload is a saved curve now: kept (see test_tcmode_and_payload), not refused.
        const std::string text =
            tc_project(30, tc_object(0, 0, "0,59", tc_row("0.000", "100.000", "0|27")) + plain_object(1, 1, "0,59"));
        const RetimeResult r = retime_target(text, rate(60));
        check(r.ok && first_row(r, "位置=") == std::string("位置=0.000,100.000,") + kTcMode + ",0|27",
              "time control with a | payload is kept", refusal_or_converted(r));
    }
    {
        const std::string text =
            tc_project(30, tc_object(0, 0, "0,59", "0.000,100.000,直線移動,0") + plain_object(1, 1, "0,59"));
        expect_refused("time control 位置 with another mode is refused", text, rate(60), line_of_first(text, "位置="),
                       kTcRefusal);
    }
    {
        const std::string text = tc_project(30, tc_object(0, 0, "0,59", "50") + plain_object(1, 1, "0,59"));
        expect_refused("time control 位置 that is not a track is refused", text, rate(60), line_of_first(text, "位置="),
                       kTcRefusal);
    }
    {
        const std::string text = tc_project(30, "[0]\nlayer=0\nframe=0,59\n[0.0]\neffect.name=時間制御(オブジェクト)\n"
                                                "繰り返し=1\nコマ落ち=1\n対象レイヤー数=1\n");
        expect_refused("time control without a 位置 row is refused at the effect line", text, rate(60),
                       line_of_first(text, "effect.name=時間制御"), kTcRefusal);
    }
    {
        // The mode is on the whitelist for every track now (test_tcmode_and_payload); an unknown mode still is not.
        const std::string text = tc_project(30, "[0]\nlayer=0\nframe=0,59\n[0.0]\neffect.name=標準描画\nX=0.000,100.000,"
                                                "直線移動(時間制御謎),0\n");
        expect_refused("a mode that only starts with 直線移動(時間制御 is not on the whitelist", text, rate(60),
                       line_of_first(text, "X="), "trackbar mode '直線移動(時間制御謎)' is not on the whitelist");
    }
    {
        // A コマ落ち beyond 64 bits cannot be held exactly: refused at its line (the Python tool uses big integers).
        const auto result = with_drop("99999999999999999999", rate(60), 30);
        const RetimeResult& r = result.second;
        check(!r.ok && r.refusal_line == line_of_first(result.first, "コマ落ち=") &&
                  contains(r.refusal_reason, "too large"),
              "コマ落ち beyond 64 bits is refused", refusal_or_converted(r));
    }
    {
        // Steps need the whole frame= list: another trackbar row in the object (it has one value per frame point) refuses.
        const std::string head = "[0]\nlayer=0\nframe=0,11\n[0.0]\neffect.name=時間制御(オブジェクト)\n位置=";
        const std::string tail = "\n繰り返し=1\nコマ落ち=1\n対象レイヤー数=1\nX=0.00,10.00,直線移動,0\n";
        const std::string reversed = tc_project(30, head + tc_row("100.000", "0.000") + tail + plain_object(1, 1, "0,11"));
        const RetimeResult r = retime_target(reversed, rate(60));
        check(!r.ok && r.refusal_line == 0 &&
                  refusal_message(r) == "REFUSED: time control at line " + std::to_string(line_of_first(reversed, "位置=")) +
                                            ": the object has other animated trackbars",
              "steps with another trackbar in the object are refused", refusal_or_converted(r));
        // The same object stays a straight line (no steps needed): the other trackbar is kept.
        const std::string forward = tc_project(30, head + tc_row("0.000", "100.000") + tail + plain_object(1, 1, "0,11"));
        const RetimeResult f = retime_target(forward, rate(60));
        check(f.ok && first_row(f, "frame=") == "frame=0,23" && first_row(f, "X=") == "X=0.00,10.00,直線移動,0",
              "a straight line next to another trackbar converts, the trackbar keeps its two values", refusal_or_converted(f));
        // A trackbar whose values are all equal is repeated at every point (written as it was).
        const std::string constant = tc_project(30, head + tc_row("100.000", "0.000") +
                                                        "\n繰り返し=1\nコマ落ち=1\n対象レイヤー数=1\nX=5.0,5.00,補間移動,0\n" +
                                                        plain_object(1, 1, "0,11"));
        const RetimeResult c = retime_target(constant, rate(60));
        if (c.ok) {
            const std::size_t points = parse_ints(object_rows(c.output, 0).at("frame")).size();
            std::string want = "X=";
            for (std::size_t i = 0; i < points; ++i) {
                want += i > 0 ? ",5.0" : "5.0";
            }
            check(points == 24 && first_row(c, "X=") == want + ",補間移動,0",
                  "steps with a constant trackbar next to them: it is repeated at the 24 points", first_row(c, "X="));
        } else {
            check(false, "steps with a constant trackbar next to them: converts", refusal_message(c));
        }
        // A 再生範囲 row has no value per frame point and does not block steps.
        const std::string range = tc_project(30, head + tc_row("100.000", "0.000") +
                                                     "\n繰り返し=1\nコマ落ち=1\n対象レイヤー数=1\nR=0,11,再生範囲,1\n" +
                                                     plain_object(1, 1, "0,11"));
        const RetimeResult g = retime_target(range, rate(60));
        check(g.ok && first_row(g, "R=") == "R=0,11,再生範囲,1" && contains(first_row(g, "位置="), "瞬間移動"),
              "steps with only a 再生範囲 row next to them convert", refusal_or_converted(g));
        // Two effects on one object: steps for one of them are refused because the other 位置 row is a trackbar.
        const std::string two = tc_project(
            30, "[0]\nlayer=0\nframe=0,59\n[0.0]\neffect.name=時間制御(オブジェクト)\n位置=" + tc_row("0.000", "100.000") +
                    "\n繰り返し=1\nコマ落ち=2\n対象レイヤー数=1\n[0.1]\neffect.name=時間制御(オブジェクト)\n位置=" +
                    tc_row("100.000", "0.000") + "\n繰り返し=1\nコマ落ち=4\n対象レイヤー数=1\n" + plain_object(1, 1, "0,59"));
        const RetimeResult t = retime_target(two, rate(60));
        check(!t.ok && contains(t.refusal_reason, ": the object has other animated trackbars") &&
                  contains(t.refusal_reason, "time control at line " + std::to_string(line_of_first(two, "位置=100.000"))),
              "steps for one of two time control effects on an object are refused", refusal_or_converted(t));
    }
    {
        // コマ落ち=2 outside a time control effect keeps the old refusal.
        const std::string text = tc_project(30, "[0]\nlayer=0\nframe=0,59\n[0.0]\neffect.name=標準描画\nコマ落ち=2\n");
        expect_refused("コマ落ち=2 outside a time control effect is refused", text, rate(60),
                       line_of_first(text, "コマ落ち=2"),
                       "コマ落ち=2 (frame drop) is only converted inside a 時間制御(オブジェクト) effect");
    }
}

void test_time_control_k1_untouched() {
    // A scene that keeps its rate (k = 1) is not touched: rows and notes stay as they were, whatever the values are.
    const std::string text = tc_project(30, tc_object(0, 0, "0,11", tc_row("-50.000", "150.000"), "3") +
                                                tc_object(1, 1, "12,12", tc_row("30.000", "60.000"), "2") +
                                                plain_object(2, 2, "0,23"));
    const RetimeResult r = retime_target(text, rate(30));
    check(r.ok && r.notes.empty() && first_row(r, "位置=") == std::string("位置=-50.000,150.000,") + kTcMode + ",0" &&
              rows_with_prefix(r, "位置=")[1] == std::string("位置=30.000,60.000,") + kTcMode + ",0" &&
              rows_with_prefix(r, "コマ落ち=")[0] == "コマ落ち=3" && rows_with_prefix(r, "コマ落ち=")[1] == "コマ落ち=2",
          "time control at k = 1: 位置 and コマ落ち are not rewritten, no note", refusal_or_converted(r));
    const RetimeResult p = free_fps::retime(text, rate(30), L"D:\\out\\retimed.aup2", true);
    check(p.ok && p.precise && p.precise->applied == 0 && p.precise->skipped.empty(),
          "time control at k = 1 with precise: nothing to re-sample", refusal_or_converted(p));
}

void test_time_control_precise() {
    // A time control row is not re-sampled and not reported: constant (25 -> 25) and moving (0 -> 100) alike, while a
    // trackbar of another object is re-sampled as usual.
    const std::string objects = tc_object(0, 0, "0,99", tc_row("25.000", "25.000")) +
                                tc_object(1, 1, "0,99", tc_row("0.000", "100.000")) +
                                "[2]\nlayer=2\nframe=0,99\n[2.0]\neffect.name=標準描画\nX=0.00,100.00,直線移動,0\n";
    const std::string text = tc_project(30, objects);
    const RetimeResult plain = retime_target(text, rate(60));
    const RetimeResult precise = free_fps::retime(text, rate(60), L"D:\\out\\retimed.aup2", true);
    check(plain.ok && precise.ok, "time control precise: converts", refusal_message(precise));
    check(precise.precise && precise.precise->applied == 1 && precise.precise->skipped.empty() &&
              precise.precise->tracks.size() == 1 && precise.precise->tracks[0].line == line_of_first(text, "X="),
          "time control precise: only the X row is re-sampled; 位置 rows are neither applied nor skipped");
    const std::vector<std::string> plain_rows = rows_with_prefix(plain, "位置=");
    const std::vector<std::string> precise_rows = rows_with_prefix(precise, "位置=");
    check(plain_rows == precise_rows && plain_rows.size() == 2 &&
              plain_rows[0] == std::string("位置=24.874,24.874,") + kTcMode + ",0" &&
              plain_rows[1] == std::string("位置=0.000,100.000,") + kTcMode + ",0",
          "time control precise: the 位置 rows are the same with and without --precise",
          plain_rows.empty() ? std::string("<none>") : plain_rows[0]);
    int position_changes = 0;
    for (const auto& change : precise.changes) {
        position_changes += change.old_text && change.old_text->compare(0, 7, "位置=") == 0 ? 1 : 0;
    }
    check(position_changes == 1, "time control precise: the changed 位置 row is recorded once",
          std::to_string(position_changes));
    // Only a time control row: precise has nothing to do.
    const std::string only =
        tc_project(30, tc_object(0, 0, "0,99", tc_row("25.000", "25.000")) + plain_object(1, 1, "0,99"));
    const RetimeResult p2 = free_fps::retime(only, rate(60), L"D:\\out\\retimed.aup2", true);
    check(p2.ok && p2.precise && p2.precise->applied == 0 && p2.precise->skipped.empty() && p2.precise->tracks.empty(),
          "time control precise: an object with only a time control row is not counted", refusal_message(p2));
}

void test_time_control_kept_scene() {
    // Main 30 FPS, sub 15 FPS. The sub scene holds a time control object that a followed scene would refuse (three
    // positions, three-point frame=) and a valid one with コマ落ち=2.
    const std::string sub_bad = tc_object(1, 0, "0,30,74", std::string("0.000,50.000,100.000,") + kTcMode + ",0", "2",
                                          "scene=1\n");
    const std::string sub_ok = tc_object(2, 1, "0,74", tc_row("100.000", "0.000"), "2", "scene=1\n");
    const std::string text = multi_scene_project(
        "display.scene=0\n", scene_block(0, "m", 30, 1, kSceneEffect) + scene_block(1, "s", 15, 1, sub_bad + sub_ok));
    const RetimeResult kept = run_policy(text, rate(60), kAuto);
    check(kept.ok && from_marker(kept.output, "[scene.1]") == from_marker(text, "[scene.1]"),
          "time control in a kept scene: byte for byte unchanged, bad rows included", refusal_or_converted(kept));
    check(kept.notes.size() == 1 && kept.notes[0] == "scene 1 kept at its own rate 15/1",
          "time control in a kept scene: no time control note", first_note(kept));
    const RetimeResult follow = run_policy(text, rate(60), kFollow);
    check(!follow.ok && follow.refusal_line == line_of_first(text, "位置=0.000,50.000") &&
              contains(follow.refusal_reason, "time control 位置 must be a two-point"),
          "the same scene followed: the bad time control is refused", refusal_or_converted(follow));
    // 15 -> 60 (k = 4): frame=0,74 becomes 0,299 and the reversed block (100 -> 0, コマ落ち=2) needs steps, so コマ落ち
    // becomes 1; the object keeps showing what the original showed on every frame (k = 4 of its own scene).
    const std::string good = multi_scene_project(
        "display.scene=0\n", scene_block(0, "m", 30, 1, kSceneEffect) + scene_block(1, "s", 15, 1, sub_ok));
    const RetimeResult f = run_policy(good, rate(60), kFollow);
    check(f.ok, "time control in a followed sub scene: converts", refusal_or_converted(f));
    if (f.ok) {
        const auto rows = object_rows(from_marker(f.output, "[scene.1]"), 2);
        TcOriginal o;
        o.s = 0;
        o.e = 74;
        o.a = 100;
        o.b = 0;
        o.drop = 2;
        o.p = 60;
        o.q = 15;
        const TcReport report = tc_compare(from_marker(f.output, "[scene.1]"), 2, o, 3);
        const std::vector<int64_t> frames = parse_ints(rows.at("frame"));
        check(report.route == 'B' && report.first_bad.empty() && frames.front() == 0 && frames.back() == 299 &&
                  rows.at("コマ落ち") == "1",
              "time control in a followed sub scene: steps over frames 0..299, コマ落ち=1, same controlled time as the "
              "original on every frame (k = 4)",
              report.first_bad);
    }
    const RetimeResult keep = run_policy(good, rate(60), kKeep);
    check(keep.ok && from_marker(keep.output, "[scene.1]") == from_marker(good, "[scene.1]"),
          "time control in a sub scene under keep: unchanged", refusal_or_converted(keep));
}


// ---------------------------------------------------------------------------
// 直線移動(時間制御) on any track, '|' payloads, curves in the time control 位置 row
// ---------------------------------------------------------------------------

std::string replace_mode(const std::string& row, const std::string& from, const std::string& to) {
    const std::size_t pos = row.find("," + from + ",");
    return pos == std::string::npos ? row : row.substr(0, pos + 1) + to + row.substr(pos + 1 + from.size());
}

void test_tcmode_and_payload() {
    const std::string tc = kTcMode;
    const std::string easing = "イージング（通常）@イージング";
    auto object = [](const std::string& frames, const std::string& rows) {
        return "[0]\nlayer=0\nframe=" + frames + "\n[0.0]\neffect.name=標準描画\n" + rows;
    };
    auto precise = [](const std::string& text, Rational target) {
        return free_fps::retime(text, target, L"D:\\out\\retimed.aup2", true);
    };
    auto first_skip = [](const RetimeResult& p) {
        return p.precise && !p.precise->skipped.empty() ? p.precise->skipped[0].reason : refusal_message(p);
    };

    // 1. The mode on an ordinary track: two points and several points, frames rescaled, values kept, equal to 直線移動.
    {
        const std::string two = project(scene30(), object("0,29", "X=-600.00,600.00," + tc + ",0\n"));
        const RetimeResult r = retime_target(two, rate(60));
        check(r.ok && first_row(r, "frame=") == "frame=0,59" && first_row(r, "X=") == "X=-600.00,600.00," + tc + ",0",
              "tcmode: a two-point track on any effect is accepted, values kept", refusal_or_converted(r));
        const RetimeResult line = retime_target(project(scene30(), object("0,29", "X=-600.00,600.00,直線移動,0\n")),
                                                rate(60));
        check(line.ok && r.changes.size() == line.changes.size(),
              "tcmode: the output has the same shape as with 直線移動", refusal_or_converted(line));
        const std::string many = project(scene30(), object("0,10,29", "X=0.00,50.00,100.00," + tc + ",0\n"));
        const RetimeResult m = retime_target(many, rate(60));
        check(m.ok && first_row(m, "frame=") == "frame=0,20,59" && first_row(m, "X=") == "X=0.00,50.00,100.00," + tc + ",0",
              "tcmode: several points are accepted, frames rescaled, values kept", refusal_or_converted(m));
        expect_refused("tcmode: the value count must equal the frame points",
                       project(scene30(), object("0,29", "X=0.00,50.00,100.00," + tc + ",0\n")), rate(60),
                       line_of_first(project(scene30(), object("0,29", "X=0.00,50.00,100.00," + tc + ",0\n")), "X="),
                       "trackbar has 3 values");
    }
    // 2. A '|' payload (which holds commas) is kept as written in 直線移動, 直線移動(時間制御) and the easing mode, and
    //    refused in every other mode with the existing text.
    {
        for (const std::string& mode : {std::string("直線移動"), tc, easing}) {
            const std::string row = "X=-600.00,600.00," + mode + ",0|1,0,1,0";
            const RetimeResult r = retime_target(project(scene30(), object("0,29", row + "\n")), rate(66));
            check(r.ok && first_row(r, "X=") == row && first_row(r, "frame=") == "frame=0,65",
                  "payload: kept as written in " + mode, refusal_or_converted(r));
        }
        const std::string flat = "Y=0.00,0.00,直線移動,0|1,0,1,0";
        const RetimeResult r = retime_target(project(scene30(), object("0,29", flat + "\n")), rate(60));
        check(r.ok && first_row(r, "Y=") == flat, "payload: an unused curve on a 直線移動 row is kept",
              refusal_or_converted(r));
        const std::string bad = project(scene30(), object("0,29", "X=0.00,100.00,補間移動,0|1,0,1,0\n"));
        expect_refused("payload: another mode with a payload is still refused", bad, rate(60), line_of_first(bad, "X="),
                       "trackbar '|' payload '1,0,1,0' is not on the whitelist");
        const std::string bad2 = project(scene30(), object("0,29", "X=0.00,100.00,瞬間移動,0|3\n"));
        expect_refused("payload: 瞬間移動 with a payload is still refused", bad2, rate(60), line_of_first(bad2, "X="),
                       "trackbar '|' payload '3' is not on the whitelist");
    }
    // 3. Precise: 直線移動 with a payload is re-sampled and the payload is appended after the flag.
    {
        const std::string text = project(scene30(), object("0,29", "X=0.00,100.00,直線移動,0|1,0,1,0\n"));
        const RetimeResult p = precise(text, rate(66));
        const std::string row = first_row(p, "X=");
        check(p.ok && p.precise && p.precise->applied == 1 && p.precise->skipped.empty() &&
                  contains(row, ",直線移動,0|1,0,1,0") && row.compare(0, 7, "X=0.00,") == 0 &&
                  first_row(p, "frame=").compare(0, 9, "frame=0,6") == 0 && first_row(p, "frame=").back() == '5',
              "precise payload: 直線移動 with a payload is re-sampled and keeps the payload", row);
        const RetimeResult same = precise(project(scene30(), object("0,29", "X=0.00,100.00,直線移動,0\n")), rate(66));
        const std::string plain_row = first_row(same, "X=");
        check(same.ok && row == plain_row + "|1,0,1,0", "precise payload: the values equal those without a payload",
              row + " / " + plain_row);
        const std::string constant = project(scene30(), object("0,29", "Y=0.00,0.00,直線移動,0|1,0,1,0\n"));
        const RetimeResult c = precise(constant, rate(60));
        check(c.ok && c.precise && c.precise->applied == 1 && contains(first_row(c, "Y="), ",直線移動,0|1,0,1,0"),
              "precise payload: a constant row with a payload is re-sampled and keeps the payload", first_row(c, "Y="));
    }
    // 4. Precise: 直線移動(時間制御). A curve or intermediate points are skipped; two points are re-sampled when the rescaled
    //    keyframes need no extra points for the end hold, and skipped when they do.
    {
        const std::string curve = project(scene30(), object("0,29", "X=0.00,100.00," + tc + ",0|1,0,1,0\n"));
        const RetimeResult p = precise(curve, rate(66));
        check(p.ok && p.precise && p.precise->applied == 0 && p.precise->skipped.size() == 1 &&
                  p.precise->skipped[0].line == line_of_first(curve, "frame=0,29") &&
                  p.precise->skipped[0].reason == "trackbar at line " + std::to_string(line_of_first(curve, "X=")) +
                                                      " uses mode " + tc + " with a curve or keyframes; it is not re-sampled" &&
                  first_row(p, "X=") == "X=0.00,100.00," + tc + ",0|1,0,1,0",
              "precise tcmode: a payload skips the object, row unchanged", first_skip(p));
        const std::string many = project(scene30(), object("0,10,29", "X=0.00,50.00,100.00," + tc + ",0\n"));
        const RetimeResult m = precise(many, rate(60));
        check(m.ok && m.precise && m.precise->applied == 0 && m.precise->skipped.size() == 1 &&
                  m.precise->skipped[0].reason == "trackbar at line " + std::to_string(line_of_first(many, "X=")) +
                                                      " uses mode " + tc + " with a curve or keyframes; it is not re-sampled",
              "precise tcmode: intermediate points skip the object", first_skip(m));
        // 30 -> 60: the rescaled end 59 is after the held end 58, so a keyframe would be added: skipped.
        const std::string up = project(scene30(), object("0,29", "X=0.00,100.00," + tc + ",0\n"));
        const RetimeResult u = precise(up, rate(60));
        check(u.ok && u.precise && u.precise->applied == 0 && u.precise->skipped.size() == 1 &&
                  u.precise->skipped[0].reason == "trackbar at line " + std::to_string(line_of_first(up, "X=")) +
                                                      " uses mode " + tc + "; end hold needs keyframes this mode ignores" &&
                  first_row(u, "X=") == "X=0.00,100.00," + tc + ",0" && first_row(u, "frame=") == "frame=0,59",
              "precise tcmode: an end hold that needs extra keyframes skips the object", first_skip(u));
        // 66 -> 60: no extra keyframes are needed: re-sampled exactly like 直線移動, the mode text is kept.
        const std::string rate66 = "video.rate=66\nvideo.scale=1\n";
        const RetimeResult d = precise(project(rate66, object("0,65", "X=0.00,100.00," + tc + ",0\n")), rate(60));
        const RetimeResult dl = precise(project(rate66, object("0,65", "X=0.00,100.00,直線移動,0\n")), rate(60));
        const std::string expected = replace_mode(first_row(dl, "X="), "直線移動", tc);
        check(d.ok && d.precise && d.precise->applied == 1 && d.precise->skipped.empty() && dl.ok &&
                  first_row(d, "X=") == expected && contains(first_row(d, "X="), "," + tc + ",0") &&
                  first_row(d, "frame=") == first_row(dl, "frame="),
              "precise tcmode: no extra keyframes needed: re-sampled like 直線移動, mode kept",
              first_row(d, "X=") + " / " + expected);
        // One bad row skips the whole object, also next to a good 直線移動 row.
        const std::string mixed =
            project(scene30(), object("0,29", "X=0.00,100.00,直線移動,0\nY=0.00,100.00," + tc + ",0\n"));
        const RetimeResult x = precise(mixed, rate(60));
        check(x.ok && x.precise && x.precise->applied == 0 && x.precise->skipped.size() == 1 &&
                  first_row(x, "X=") == "X=0.00,100.00,直線移動,0",
              "precise tcmode: one such row skips the whole object", first_skip(x));
    }
    // 4b. 補間移動(時間制御): accepted on any track, with or without a payload, kept as written; precise never re-samples it.
    {
        const std::string im = "補間移動(時間制御)";
        const std::string row = "X=0.00,50.00,100.00," + im + ",0|1,0,1,0";
        const RetimeResult r = retime_target(project(scene30(), object("0,10,29", row + "\n")), rate(60));
        check(r.ok && first_row(r, "X=") == row && first_row(r, "frame=") == "frame=0,20,59",
              "interp tcmode: accepted with a payload and middle points, kept", refusal_or_converted(r));
        const std::string plain = project(scene30(), object("0,29", "X=0.00,100.00," + im + ",0\n"));
        const RetimeResult p = precise(plain, rate(66));
        check(p.ok && p.precise && p.precise->applied == 0 && p.precise->skipped.size() == 1 &&
                  p.precise->skipped[0].reason ==
                      "trackbar at line " + std::to_string(line_of_first(plain, "X=")) + " uses mode " + im +
                          "; it is not re-sampled",
              "interp tcmode: precise skips the object", first_skip(p));
        const std::string constant = project(scene30(), object("0,29", "X=5.00,5.00," + im + ",0|1,0,1,0\n"));
        const RetimeResult c = precise(constant, rate(66));
        check(c.ok && c.precise && c.precise->applied == 0 && c.precise->skipped.size() == 1 &&
                  contains(c.precise->skipped[0].reason, "; it is not re-sampled"),
              "interp tcmode: a constant row with a payload is skipped as well", first_skip(c));
    }
    // 4c. Order of the precise checks: a problem of a later row (zero length, payload on another mode is refused
    //     earlier) comes before the end hold check of an earlier 直線移動(時間制御) row.
    {
        const std::string text = project(scene30(), object("5,5", "X=0.00,100.00," + tc + ",0\n"));
        const RetimeResult p = precise(text, rate(60));
        check(p.ok && p.precise && p.precise->skipped.size() == 1 &&
                  p.precise->skipped[0].reason == "frame= start equals end; there is no segment to resample",
              "precise tcmode: the zero-length reason comes before the end hold reason", first_skip(p));
    }
    // 5. The time control 位置 row with a curve: kept as written, frames converted, no A/B rewrite.
    {
        const std::string row = tc_row("0.000", "100.000", "0|0,0,1,0");
        const std::string text = tc_project(30, tc_object(0, 0, "0,59", row) + plain_object(1, 1, "0,59"));
        const RetimeResult r = retime_target(text, rate(60));
        check(r.ok && first_row(r, "位置=") == "位置=" + row && first_row(r, "frame=") == "frame=0,119" &&
                  first_row(r, "コマ落ち=") == "コマ落ち=1" && r.notes.size() == 1 &&
                  r.notes[0] == "time control at line " + std::to_string(line_of_first(text, "frame=0,59")) +
                                    ": 位置 has a curve; kept (object-relative, within one frame)",
              "time control curve: 位置 row kept, frames converted, one note", refusal_or_converted(r) + " / " + first_note(r));
        const RetimeResult p = precise(text, rate(60));
        check(p.ok && p.precise && p.precise->applied == 0 && p.precise->skipped.empty() &&
                  first_row(p, "位置=") == "位置=" + row,
              "time control curve: precise leaves the row alone and does not report it", refusal_or_converted(p));
        // 位置 with a curve, other values: also at a rate that is not an integer multiple.
        const RetimeResult q = retime_target(text, rate(66));
        check(q.ok && first_row(q, "位置=") == "位置=" + row && first_row(q, "frame=") == "frame=0,131",
              "time control curve: 30 -> 66", refusal_or_converted(q));
        // k = 1 leaves the effect alone, with no note.
        const RetimeResult same = retime_target(text, rate(30));
        check(same.ok && same.notes.empty() && first_row(same, "位置=") == "位置=" + row, "time control curve: k = 1 is untouched",
              refusal_or_converted(same));
    }
    // 6. コマ落ち with a curve: K x k and s x k must be integers, then K x k is written; otherwise REFUSED.
    {
        const std::string row = tc_row("0.000", "100.000", "0|0,0,1,0");
        const std::string text = tc_project(30, tc_object(0, 0, "0,59", row, "2") + plain_object(1, 1, "0,59"));
        const RetimeResult r = retime_target(text, rate(60));
        check(r.ok && first_row(r, "コマ落ち=") == "コマ落ち=4" && first_row(r, "位置=") == "位置=" + row &&
                  r.notes.size() == 1 && contains(r.notes[0], "位置 has a curve; kept (object-relative, within one frame)") &&
                  contains(r.notes[0], "コマ落ち 2 -> 4"),
              "time control curve: コマ落ち=2 at 30 -> 60 becomes 4", refusal_or_converted(r) + " / " + first_note(r));
        const RetimeResult f = retime_target(text, rate(66));
        check(!f.ok && f.refusal_line == 0 &&
                  refusal_message(f) == "REFUSED: time control at line " + std::to_string(line_of_first(text, "位置=")) +
                                            " has a curve and コマ落ち=2 cannot be kept exact",
              "time control curve: コマ落ち=2 at 30 -> 66 (K x k = 4.4) is refused", refusal_or_converted(f));
        // k = 3/2: K x k = 3 for K = 2. s x k must be an integer as well: s = 2 passes, s = 1 does not.
        const std::string ok_s = tc_project(30, tc_object(0, 0, "2,59", row, "2") + plain_object(1, 1, "0,59"));
        const RetimeResult o = retime_target(ok_s, rate(45));
        check(o.ok && first_row(o, "コマ落ち=") == "コマ落ち=3", "time control curve: s x k and K x k integers: コマ落ち=3",
              refusal_or_converted(o));
        const std::string bad_s = tc_project(30, tc_object(0, 0, "1,59", row, "2") + plain_object(1, 1, "0,59"));
        const RetimeResult b = retime_target(bad_s, rate(45));
        check(!b.ok && contains(b.refusal_reason, "has a curve and コマ落ち=2 cannot be kept exact"),
              "time control curve: a non-integer s x k is refused", refusal_or_converted(b));
        // A kept scene (sub scene at its own rate) is not looked at.
        const RetimeResult k1 = retime_target(text, rate(30));
        check(k1.ok, "time control curve: k = 1 with コマ落ち=2 converts untouched", refusal_or_converted(k1));
    }
    // 7. A 直線移動(時間制御) row next to a straight time control 位置 row (route A) is kept; in route B it follows the
    //    existing rule (changing values refuse).
    {
        const std::string text = tc_project(30, "[0]\nlayer=0\nframe=0,59\n[0.0]\neffect.name=時間制御(オブジェクト)\n位置=" +
                                                    tc_row("0.000", "100.000") + "\nY=0.000,1.000," + tc + ",0\n");
        const RetimeResult r = retime_target(text, rate(60));
        check(r.ok && first_row(r, "Y=") == "Y=0.000,1.000," + tc + ",0", "tcmode: a second row inside a time control effect",
              refusal_or_converted(r));
    }
}

// ---------------------------------------------------------------------------
// Frame-counting modes (反復移動, ランダム移動, 移動量指定). Rules and texts as in the Python tool.
// ---------------------------------------------------------------------------

// One 標準描画 object on layer 0 with the given frame= points, followed by its trackbar rows (each ends with '\n').
std::string frame_mode_object(const std::string& rows, const std::string& frames = "0,29") {
    return plain_object(0, 0, frames) + rows;
}

void test_frame_count_modes() {
    const std::string repeat = "反復移動";
    const std::string random = "ランダム移動";
    const std::string step = "移動量指定";
    const std::string x_row = "X=-600.00,600.00,反復移動,4|15\n";

    // 1. 反復移動 30 -> 60: the parameter doubles (15 -> 30), the values stay; Y and Z change as well. One note per changed row.
    {
        const std::string text = project(scene30(), frame_mode_object(x_row + "Y=0.00,0.00,反復移動,4|15\n"));
        const RetimeResult r = retime_target(text, rate(60));
        const std::string x_line = std::to_string(line_of_first(text, "X="));
        const std::string y_line = std::to_string(line_of_first(text, "Y="));
        check(r.ok && first_row(r, "frame=") == "frame=0,59" && first_row(r, "X=") == "X=-600.00,600.00,反復移動,4|30" &&
                  first_row(r, "Y=") == "Y=0.00,0.00,反復移動,4|30",
              "frame modes: 反復移動 30 -> 60 doubles the parameter, values kept", refusal_or_converted(r));
        check(r.notes.size() == 2 && r.notes[0] == "trackbar at line " + x_line + ": 反復移動 parameter 15 -> 30" &&
                  r.notes[1] == "trackbar at line " + y_line + ": 反復移動 parameter 15 -> 30",
              "frame modes: 反復移動 30 -> 60 notes", r.notes.empty() ? std::string("<no notes>") : r.notes[0]);
    }
    // 2. 反復移動 30 -> 60000/1001: 15 x 2000/1001 = 29.97002997..., written with 6 decimals and trailing zeros removed.
    {
        const RetimeResult r = retime_target(project(scene30(), frame_mode_object(x_row)), rate(60000, 1001));
        check(r.ok && first_row(r, "X=") == "X=-600.00,600.00,反復移動,4|29.97003" && first_row(r, "frame=") == "frame=0,59",
              "frame modes: 反復移動 30 -> 60000/1001 writes 29.97003", refusal_or_converted(r));
    }
    // 3. 反復移動 30 -> 66 (k = 11/5): 7 -> 15.4, a decimal parameter; 15.0 is read as 15.
    {
        const RetimeResult r = retime_target(project(scene30(), frame_mode_object("X=-600.00,600.00,反復移動,4|7\n")), rate(66));
        const RetimeResult d = retime_target(project(scene30(), frame_mode_object("X=-600.00,600.00,反復移動,4|15.0\n")), rate(60));
        check(r.ok && first_row(r, "X=") == "X=-600.00,600.00,反復移動,4|15.4" && first_row(r, "frame=") == "frame=0,65",
              "frame modes: 反復移動 30 -> 66 writes 15.4 for 7", refusal_or_converted(r));
        check(d.ok && first_row(d, "X=") == "X=-600.00,600.00,反復移動,4|30",
              "frame modes: a parameter 15.0 is the integer 15", refusal_or_converted(d));
    }
    // 4. ランダム移動 30 -> 66: 15 x 11/5 = 33, an integer, kept as an integer.
    {
        const RetimeResult r = retime_target(project(scene30(), frame_mode_object("X=-600.00,600.00,ランダム移動,4|15\n")), rate(66));
        check(r.ok && first_row(r, "X=") == "X=-600.00,600.00,ランダム移動,4|33", "frame modes: ランダム移動 30 -> 66 writes 33",
              refusal_or_converted(r));
        const RetimeResult d = retime_target(project(scene30(), frame_mode_object("X=-600.00,600.00,ランダム移動,4|15.0\n")), rate(60));
        check(d.ok && first_row(d, "X=") == "X=-600.00,600.00,ランダム移動,4|30",
              "frame modes: ランダム移動 with 15.0 is the integer 15", refusal_or_converted(d));
    }
    // 5. ランダム移動 refuses a parameter that is not an integer at the target rate (15 x 2000/1001 = 30000/1001), and one
    //    that is not an integer itself (15.4).
    {
        const std::string text = project(scene30(), frame_mode_object("X=-600.00,600.00,ランダム移動,4|15\n"));
        expect_refused("frame modes: ランダム移動 30 -> 60000/1001 is refused", text, rate(60000, 1001),
                       line_of_first(text, "X="),
                       "ランダム移動 parameter 15 becomes 30000/1001 at the target rate; a non-integer parameter is not verified");
        const std::string frac = project(scene30(), frame_mode_object("X=-600.00,600.00,ランダム移動,4|15.4\n"));
        expect_refused("frame modes: ランダム移動 15.4 is refused", frac, rate(60), line_of_first(frac, "X="),
                       "ランダム移動 parameter must be an integer, got '15.4'");
    }
    // 5b. ランダム移動 is refused for an object that does not start at frame 0 (its random path depends on the start frame).
    //     The same row at the source rate is not checked, and 反復移動 is not limited by this rule.
    {
        const std::string late = project(scene30(), frame_mode_object("X=-600.00,600.00,ランダム移動,4|15\n", "10,149"));
        expect_refused("frame modes: ランダム移動 starting at frame 10 is refused", late, rate(60), line_of_first(late, "X="),
                       "ランダム移動 is accepted only for objects that start at frame 0 (its random path depends on the start frame)");
        const RetimeResult same = retime_target(late, rate(30));
        check(same.ok && first_row(same, "X=") == "X=-600.00,600.00,ランダム移動,4|15", "frame modes: ランダム移動 at the source rate is not refused",
              refusal_or_converted(same));
        const RetimeResult from10 = retime_target(
            project(scene30(), frame_mode_object("X=-600.00,600.00,反復移動,4|15\n", "10,149")), rate(60));
        check(from10.ok && first_row(from10, "X=") == "X=-600.00,600.00,反復移動,4|30", "frame modes: 反復移動 starting at frame 10 is converted",
              refusal_or_converted(from10));
    }
    // 6. 移動量指定 30 -> 60: the step is halved (4.00 -> 2.00), written with the decimals of the original; Y and Z stay (0.00).
    {
        const std::string text = project(scene30(), frame_mode_object("X=-600.00,4.00,移動量指定,4\nY=0.00,0.00,移動量指定,4\n"
                                                                      "Z=0.00,0.00,移動量指定,4\n"));
        const RetimeResult r = retime_target(text, rate(60));
        check(r.ok && first_row(r, "X=") == "X=-600.00,2.00,移動量指定,4" && first_row(r, "Y=") == "Y=0.00,0.00,移動量指定,4" &&
                  first_row(r, "Z=") == "Z=0.00,0.00,移動量指定,4" && r.notes.size() == 1 &&
                  r.notes[0] == "trackbar at line " + std::to_string(line_of_first(text, "X=")) + ": 移動量指定 step 4.00 -> 2.00",
              "frame modes: 移動量指定 30 -> 60 halves the step, a zero step stays unchanged and is not noted",
              refusal_or_converted(r));
        const RetimeResult whole = retime_target(project(scene30(), frame_mode_object("X=-600,4,移動量指定,4\n")), rate(60));
        check(whole.ok && first_row(whole, "X=") == "X=-600,2,移動量指定,4", "frame modes: a step without decimals is written without them",
              refusal_or_converted(whole));
    }
    // 7. 移動量指定 30 -> 66: 4 x 5/11 = 20/11, written with the 2 decimals of the original (1.82). Over 0,29 (65 frames) the
    //    error (1.82 - 20/11) x 65 = 0.118 is accepted; over 0,149 (329 frames) it is 0.598 and refused.
    {
        const RetimeResult r = retime_target(project(scene30(), frame_mode_object("X=-600.00,4.00,移動量指定,4\n")), rate(66));
        check(r.ok && first_row(r, "X=") == "X=-600.00,1.82,移動量指定,4", "frame modes: 移動量指定 30 -> 66 writes 1.82",
              refusal_or_converted(r));
        const std::string long_text = project(scene30(), frame_mode_object("X=-600.00,4.00,移動量指定,4\n", "0,149"));
        expect_refused("frame modes: 移動量指定 30 -> 66 over 150 frames is refused", long_text, rate(66),
                       line_of_first(long_text, "X="), "移動量指定 step 4.00 cannot be written with 2 decimals within 0.5");
    }
    // 7b. 移動量指定 30 -> 60000/1001: 4 x 1001/2000 = 2.002, written as 2.00; over 0,149 (299 frames) the error is 0.598: refused.
    {
        const std::string text = project(scene30(), frame_mode_object("X=-600.00,4.00,移動量指定,4\n", "0,149"));
        expect_refused("frame modes: 移動量指定 30 -> 60000/1001 with 4.00 is refused", text, rate(60000, 1001),
                       line_of_first(text, "X="), "移動量指定 step 4.00 cannot be written with 2 decimals within 0.5");
    }
    // 8. Error bound: 反復移動 30 -> 10 (k = 1/3) writes 0.333333 for 1, which is within half a frame for small values and not
    //    for large ones.
    {
        const RetimeResult ok = retime_target(project(scene30(), frame_mode_object("X=-600.00,600.00,反復移動,4|1\n")), rate(10));
        check(ok.ok && first_row(ok, "X=") == "X=-600.00,600.00,反復移動,4|0.333333", "frame modes: 0.333333 is within half a frame",
              refusal_or_converted(ok));
        const std::string big = project(scene30(), frame_mode_object("X=-60000.00,60000.00,反復移動,4|1\n"));
        expect_refused("frame modes: a parameter that cannot be written within half a frame is refused", big, rate(10),
                       line_of_first(big, "X="), "反復移動 parameter 1 cannot be written with 6 decimals within 0.5");
    }
    // 9. Format rules, each refused on the trackbar's line with the first rule that fails.
    {
        const std::string frames3 = "0,15,29";
        const std::string xx = "X=-600.00,600.00,";
        const std::string bad_bar = "移動量指定 takes no '|' payload, got '4|2'";
        const std::string points = "is accepted only with two frame points and two values (middle points are not verified)";
        struct Case {
            std::string name;
            std::string text;
            std::string reason;
        };
        const std::vector<Case> cases = {
            {"a '|' on 移動量指定", project(scene30(), frame_mode_object("X=-600.00,4.00,移動量指定,4|2\n")), bad_bar},
            {"no '|' on 反復移動", project(scene30(), frame_mode_object(xx + "反復移動,4\n")),
             "反復移動 needs a '|' parameter, got '4'"},
            {"flag 5", project(scene30(), frame_mode_object(xx + "反復移動,5|15\n")), "反復移動 flag must be 4, got '5'"},
            {"flag 5 on ランダム移動", project(scene30(), frame_mode_object(xx + "ランダム移動,5|15\n")),
             "ランダム移動 flag must be 4, got '5'"},
            {"three frame points", project(scene30(), frame_mode_object(xx + "反復移動,4|15\n", frames3)),
             "反復移動 " + points},
            {"three values", project(scene30(), frame_mode_object("X=-600.00,0.00,600.00,反復移動,4|15\n")),
             "反復移動 " + points},
            {"no frame= line", project(scene30(), "[0]\nlayer=0\n[0.0]\neffect.name=標準描画\n" + xx + "反復移動,4|15\n"),
             "反復移動 " + points},
            {"parameter 0.5", project(scene30(), frame_mode_object(xx + "反復移動,4|0.5\n")),
             "反復移動 parameter must be a number of at least 1, got '0.5'"},
            {"parameter abc", project(scene30(), frame_mode_object(xx + "反復移動,4|abc\n")),
             "反復移動 parameter must be a number of at least 1, got 'abc'"},
            {"parameter 0 on ランダム移動", project(scene30(), frame_mode_object(xx + "ランダム移動,4|0\n")),
             "ランダム移動 parameter must be a number of at least 1, got '0'"},
            {"empty parameter", project(scene30(), frame_mode_object(xx + "反復移動,4|\n")),
             "反復移動 parameter must be a number of at least 1, got ''"},
            {"no '|' with Japanese text", project(scene30(), frame_mode_object(xx + "反復移動,全角\n")),
             "反復移動 needs a '|' parameter, got '全角'"},
            {"the first rule wins", project(scene30(), frame_mode_object("X=-600.00,4.00,移動量指定,5|1\n")),
             "移動量指定 takes no '|' payload, got '5|1'"},
        };
        for (const Case& c : cases) {
            expect_refused("frame modes: refused, " + c.name, c.text, rate(60), line_of_first(c.text, "X="), c.reason);
        }
    }
    // 10. 時間制御(オブジェクト) overlap: a frame-counting row of a scene whose time control object has an overlapping frame
    //     range is refused. The reason names the time control object with the smallest frame= line.
    {
        const std::string text = tc_project(30, tc_object(0, 1, "0,29", tc_row("0.000", "100.000")) +
                                                    plain_object(1, 0, "0,29") + x_row);
        expect_refused("frame modes: overlap with a 時間制御 object is refused", text, rate(60), line_of_first(text, "X="),
                       "反復移動 is not verified together with 時間制御(オブジェクト) (the time control object at line " +
                           std::to_string(line_of_first(text, "frame=0,29\n[0.0]\neffect.name=時間制御")) + " overlaps it)");
        const std::string apart = tc_project(30, tc_object(0, 1, "0,9", tc_row("0.000", "100.000")) +
                                                     plain_object(1, 0, "10,29") + x_row);
        const RetimeResult r = retime_target(apart, rate(60));
        check(r.ok && first_row(r, "X=") == "X=-600.00,600.00,反復移動,4|30", "frame modes: no overlap, converted",
              refusal_or_converted(r));
        // The overlap rule comes before the ランダム移動 start-frame rule: the overlap reason is the one given.
        const std::string late_overlap = tc_project(30, tc_object(0, 1, "0,29", tc_row("0.000", "100.000")) +
                                                             plain_object(1, 0, "10,149") +
                                                             "X=-600.00,600.00,ランダム移動,4|15\n");
        expect_refused("frame modes: overlap is checked before the ランダム移動 start-frame rule", late_overlap, rate(60),
                       line_of_first(late_overlap, "X="),
                       "ランダム移動 is not verified together with 時間制御(オブジェクト) (the time control object at line " +
                           std::to_string(line_of_first(late_overlap, "frame=0,29\n[0.0]\neffect.name=時間制御")) + " overlaps it)");
        const std::string two = tc_project(30, tc_object(2, 1, "0,29", tc_row("0.000", "100.000")) +
                                                   tc_object(1, 2, "0,29", tc_row("0.000", "100.000")) +
                                                   plain_object(3, 0, "0,29") + x_row);
        expect_refused("frame modes: with two time control objects the smaller frame= line is named", two, rate(60),
                       line_of_first(two, "X="),
                       "(the time control object at line " +
                           std::to_string(line_of_first(two, "frame=0,29\n[2.0]\neffect.name=時間制御")) + " overlaps it)");
    }
    // 11. k = 1: the rows are not rewritten and no note is written, and the time control overlap is not checked.
    {
        const std::string text = tc_project(30, tc_object(0, 1, "0,29", tc_row("0.000", "100.000")) +
                                                    plain_object(1, 0, "0,29") + x_row + "Y=0.00,4.00,移動量指定,4\n");
        const RetimeResult r = retime_target(text, rate(30));
        check(r.ok && first_row(r, "X=") == "X=-600.00,600.00,反復移動,4|15" && first_row(r, "Y=") == "Y=0.00,4.00,移動量指定,4" &&
                  r.notes.empty(),
              "frame modes: k = 1 leaves the rows and notes alone", refusal_or_converted(r));
    }
    // 13. Exact error test with a 6-decimal parameter (the overflow case of the probe C7_反復_30_6位小数): 7.123457 at
    //     30 -> 132 (k = 22/5) and 30 -> 60000/1001 are converted with the values the Python tool writes.
    {
        const std::string text = project(scene30(), frame_mode_object("X=-600.00,600.00,反復移動,4|7.123457\n"
                                                                      "Y=0.00,0.00,反復移動,4|7.123457\n"
                                                                      "Z=0.00,0.00,反復移動,4|7.123457\n",
                                                                      "0,149"));
        const RetimeResult r = retime_target(text, rate(132));
        check(r.ok && first_row(r, "frame=") == "frame=0,659" && first_row(r, "X=") == "X=-600.00,600.00,反復移動,4|31.343211" &&
                  first_row(r, "Z=") == "Z=0.00,0.00,反復移動,4|31.343211" && r.notes.size() == 3 &&
                  r.notes[0] == "trackbar at line " + std::to_string(line_of_first(text, "X=")) +
                                    ": 反復移動 parameter 7.123457 -> 31.343211",
              "frame modes: 反復移動 7.123457 at 30 -> 132 (exact bound, no overflow)", refusal_or_converted(r));
        const RetimeResult s = retime_target(text, rate(60000, 1001));
        check(s.ok && first_row(s, "frame=") == "frame=0,299" && first_row(s, "X=") == "X=-600.00,600.00,反復移動,4|14.232681" &&
                  first_row(s, "Y=") == "Y=0.00,0.00,反復移動,4|14.232681",
              "frame modes: 反復移動 7.123457 at 30 -> 60000/1001 (exact bound, no overflow)", refusal_or_converted(s));
    }
    // 12. Precise mode: an object with a frame-counting row is skipped with the mode reason; its rows are still converted.
    {
        const std::string text = project(scene30(), frame_mode_object(x_row));
        const RetimeResult p = free_fps::retime(text, rate(60), L"D:\\out\\retimed.aup2", true);
        check(p.ok && p.precise && p.precise->applied == 0 && p.precise->skipped.size() == 1 &&
                  p.precise->skipped[0].line == line_of_first(text, "frame=0,29") &&
                  p.precise->skipped[0].reason == "trackbar at line " + std::to_string(line_of_first(text, "X=")) +
                                                      " uses mode 反復移動; frame-counting modes are not re-sampled",
              "frame modes: precise skips the object with the mode reason",
              p.precise && !p.precise->skipped.empty() ? p.precise->skipped[0].reason : refusal_message(p));
        check(frame_line(p, "X=") == "X=-600.00,600.00,反復移動,4|30", "frame modes: the skipped object's row is still converted",
              frame_line(p, "X="));
        const RetimeResult s = free_fps::retime(project(scene30(), frame_mode_object("X=0.00,4.00,移動量指定,4\n")), rate(60),
                                                L"D:\\out\\retimed.aup2", true);
        check(s.ok && s.precise && s.precise->skipped.size() == 1 &&
                  s.precise->skipped[0].reason.find("uses mode 移動量指定; frame-counting modes are not re-sampled") !=
                      std::string::npos,
              "frame modes: precise skip reason for 移動量指定", s.precise && !s.precise->skipped.empty()
                                                                    ? s.precise->skipped[0].reason
                                                                    : refusal_message(s));
    }
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
    test_precise_hold();
    test_subscene_main30_sub60();
    test_subscene_main30_sub15();
    test_subscene_display_scene();
    test_subscene_single_scene_unchanged();
    test_subscene_plan_order_and_comparison();
    test_subscene_refusal_keeps_plan_and_keep_is_untouched();
    test_subscene_kept_scene_is_opaque();
    test_subscene_column_drop_refusal();
    test_subscene_first_name_and_duplicate_rate_lines();
    test_time_control_positions();
    test_time_control_two_blocks();
    test_easing_modes();
    test_time_control_two_effects_one_object();
    test_time_control_frame_drop();
    test_time_control_steps();
    test_time_control_k1_untouched();
    test_time_control_refusals();
    test_time_control_precise();
    test_time_control_kept_scene();
    test_tcmode_and_payload();
    test_frame_count_modes();
    std::printf("%s (%d failure%s)\n", g_failures == 0 ? "ALL PASS" : "FAILED", g_failures,
                g_failures == 1 ? "" : "s");
    return g_failures == 0 ? 0 : 1;
}
