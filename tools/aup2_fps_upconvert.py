#!/usr/bin/env python3
"""Retime an AviUtl2 .aup2 project to a new scene frame rate.

Give either an integer factor K (target rate = exported scene rate * K) or an exact target rate with --target-fps
(for example 66 or 60000/1001). All rate and frame arithmetic is exact (fractions.Fraction).

Scenes may have different rates. The exported scene ([project] display.scene) and, by --subscene-rate, the other
scenes follow the target rate, each with its own ratio; a kept scene is copied byte for byte.

The 位置 of a 時間制御(オブジェクト) effect is recomputed exactly (always): a two-point line when that is exact, else a
staircase of 瞬間移動 steps; its コマ落ち K becomes K x k when the line is kept; both are measured behaviours of AviUtl2
v2.1.12 (see tools/aup2_fps_upconvert.md, "Time control"). With --precise a track holds its end value after the original
object ends (extra keyframes where needed).

Writes a new file; never modifies the input. Rules and refusals are described in
tools/aup2_fps_upconvert.md. Exit codes: 0 converted, 2 refused or usage error.
"""

import argparse
import hashlib
import json
import math
import os
import re
import sys
from collections import namedtuple
from fractions import Fraction

# ---------------------------------------------------------------------------
# Whitelists and deny-lists (see docs/aup2_fps_upconvert_feasibility.md, section 2 and 6)
# ---------------------------------------------------------------------------

# [scene.N] keys that are known and need no special handling.
SCENE_KNOWN_KEYS = frozenset({
    "scene", "name", "label",
    "video.width", "video.height", "video.rate", "video.scale",
    "audio.rate", "output.file",
    "cursor.frame", "cursor.layer",
    "preview.frame",
    "display.frame", "display.layer", "display.zoom", "display.order", "display.camera",
    "display.grid.x", "display.grid.y", "display.grid.width", "display.grid.height",
    "display.grid.step", "display.grid.range",
    "display.tempo.bpm", "display.tempo.beat", "display.tempo.start", "display.tempo.offset",
})

# [scene.N] keys whose value is a frame number; mapped with round_half_up(v * k).
SCENE_FRAME_KEYS = frozenset({"cursor.frame", "preview.frame", "display.frame"})

# Unknown scene keys containing one of these substrings cause a refusal.
SCENE_UNKNOWN_REFUSE_MARKERS = ("frame", "mark", "range")

# Trackbar move modes accepted in effect sections. 再生範囲 is seconds-based and is not rescaled.
TRACK_MODES = frozenset({
    "直線移動", "補間移動", "瞬間移動", "移動無し", "回転", "再生範囲",
    # Script-defined easing (@イージング.tra): its value depends only on the progress inside the object, the easing
    # parameter and the two end values, not on frame number, frame rate or time. Frames are rescaled and values are kept
    # like 直線移動. イージング（補間移動）@イージング is not listed: it reads obj.getpoint("time") and is not verified.
    "イージング（通常）@イージング",
})
# Trackbar payloads after '|' (e.g. "0|27"). Meaning is undocumented, so none are accepted ...
TRACK_PIPE_PAYLOADS = frozenset()
# ... except in these modes, where any payload is kept as it is (the easing parameter, e.g. "0|27").
TRACK_PAYLOAD_MODES = frozenset({"イージング（通常）@イージング"})
# Modes exempt from the "value count equals frame count" check (study section 2).
TRACK_COUNT_EXEMPT_MODES = frozenset({"再生範囲"})
# --precise: a track is resampled exactly when it is this mode with this parameter, or when its values are constant.
PRECISE_LINEAR_MODE = "直線移動"
PRECISE_LINEAR_PARAM = "0"
# --precise: tracks in these modes are left unchanged and do not make their object ineligible.
PRECISE_EXEMPT_MODES = frozenset({"再生範囲"})
# --precise: an object with exactly two frame= points that are equal has no segment; it is skipped with this reason
# (the same text as src/x264guiEx-FreeFPS/x264guiEx/freefps_export/retime.cpp precise_problem()).
PRECISE_ZERO_LENGTH_REASON = "frame= start equals end; there is no segment to resample"
# --precise: a track whose quantization error is above this share of its value range gets a note.
PRECISE_NOTE_RATIO = Fraction(1, 10000)  # 0.01 %

# Effect names that cannot be converted safely. Empty: カウンター is time-based (速度 = counts per second).
REFUSED_EFFECT_NAMES = frozenset()
# Effect whose text value may hold script between '<?' and '?>'.
TEXT_EFFECT_NAME = "テキスト"
# Effect whose 速度 must be a plain number (checked, then passed through).
COUNTER_EFFECT_NAME = "カウンター"
# Frame-dependent script markers. Time-based uses (obj.time, obj.totaltime, obj.getvalue) are not listed.
RE_FRAME_MARKER = re.compile(
    r"\bobj\.(?:frame|totalframe|index|rand)\b|(?<![\w.])rand\s*\(|\bmath\.random\s*\(|\bmath\.randomseed\b")
# Script part of a テキスト value: text from '<?' to '?>' (or to the end when unterminated).
RE_TEXT_SCRIPT = re.compile(r"<\?(.*?)(?:\?>|\Z)", re.DOTALL)
# 時間制御(オブジェクト): the effect section holds 位置=a,b,直線移動(時間制御),flag (percent of the object's own range
# at its first and last frame), 繰り返し, コマ落ち and 対象レイヤー数. Measured in AviUtl2 v2.1.12 (see
# docs/KNOWN_LIMITATIONS.md): the displayed timeline frame of the controlled layers at frame f is
# s + clamp(p(f), 0, 100) / 100 * (L - 1), with p linear from a to b over the object's frames s..e and L = e - s + 1.
TIME_CONTROL_EFFECT_NAME = "時間制御(オブジェクト)"
TIME_CONTROL_KEY_POSITION = "位置"
# The one trackbar mode that is accepted only in the 位置 line of a time control effect; it is not in TRACK_MODES.
TIME_CONTROL_MODE = "直線移動(時間制御)"
TIME_CONTROL_POSITION_REASON = "time control 位置 must be a two-point 直線移動(時間制御) track"
# コマ落ち (frame drop) is an integer field (-/+ spin box): K means one update every K project frames, 1 is no drop.
# K >= 2 is converted only in a time control effect (K x k with a two-point line, 1 with a staircase).
KEY_FRAME_DROP = "コマ落ち"
# A staircase 位置 is a 瞬間移動 track: the value of point i holds on [p_i, p_(i+1)) and the last point is never shown
# (measured, AviUtl2 v2.1.12). At most this many points are written, first and last included.
TIME_CONTROL_STEP_MODE = "瞬間移動"
TIME_CONTROL_STEP_FLAG = "0"
TIME_CONTROL_MAX_POINTS = 4096
TIME_CONTROL_LAST_FRAME_NOTE = "last frame shows the previous step"

UTF8_BOM = b"\xef\xbb\xbf"
SUFFIX_JSON = ".convert.json"

# --subscene-rate: what happens to scenes other than the exported one (the [project] display.scene).
#   auto:   a scene follows the target rate when its rate is >= the exported scene's rate, else it is kept as is;
#   follow: every scene follows the target rate;
#   keep:   every scene except the exported one is kept byte for byte.
SUBSCENE_RATE_POLICIES = ("auto", "follow", "keep")

RE_INT = re.compile(r"^\d+$")
RE_NUMBER = re.compile(r"^-?\d+(\.\d+)?$")
RE_TARGET_FPS = re.compile(r"^(\d+)(?:/(\d+))?$")
RE_OBJECT_HDR = re.compile(r"^\[(\d+)\]$")
RE_EFFECT_HDR = re.compile(r"^\[(\d+)\.(\d+)\]$")
RE_SCENE_HDR = re.compile(r"^\[scene\.(\d+)\]$")

# One scene's plan: rate_src is the scene's own rate; for a followed scene k = target rate / rate_src, for a kept
# scene k is 1 and nothing of the scene is rewritten.
ScenePlan = namedtuple("ScenePlan", "scene name rate_src follow k exported")


class Refusal(Exception):
    def __init__(self, line_no, reason):
        super().__init__(reason)
        self.line_no = line_no
        self.reason = reason

    def message(self):
        if self.line_no is None:
            return "REFUSED: %s" % self.reason
        return "REFUSED: line %d: %s" % (self.line_no, self.reason)


def sha256_hex(data):
    return hashlib.sha256(data).hexdigest()


def say(text, err=False):
    """Write a message as UTF-8 regardless of the console codepage (Japanese names are common)."""
    stream = sys.stderr if err else sys.stdout
    stream.buffer.write((text + "\n").encode("utf-8"))
    stream.flush()


def split_records(text):
    """Split text into (body, eol) pairs; eol is '\\r\\n', '\\n' or '' (last line)."""
    records = []
    for raw in re.findall(r"[^\n]*\n|[^\n]+", text):
        if raw.endswith("\n"):
            body, eol = raw[:-1], "\n"
        else:
            body, eol = raw, ""
        if body.endswith("\r"):
            body, eol = body[:-1], "\r" + eol
        records.append((body, eol))
    return records


def is_trackbar(value):
    """True for 'v1,v2,...,mode,param' values with at least two numeric values."""
    parts = value.split(",")
    if len(parts) < 4:
        return False
    if not all(RE_NUMBER.match(p) for p in parts[:-2]):
        return False
    return not RE_NUMBER.match(parts[-2])


def parse_frames(value, line_no):
    items = value.split(",")
    if len(items) < 2 or not all(RE_INT.match(x) for x in items):
        raise Refusal(line_no, "frame= must be 2 or more non-negative integers, got %r" % value)
    return [int(x) for x in items]


def parse_target_fps(text):
    """Parse --target-fps. Returns (numerator, denominator) in lowest terms.

    Raises ValueError with the reason (the caller adds the REFUSED prefix).
    """
    if "." in text:
        raise ValueError("%r is a decimal rate and is refused; give the exact fraction, "
                         "for example 60000/1001" % text)
    match = RE_TARGET_FPS.match(text)
    if not match:
        raise ValueError("must be a positive integer or a fraction such as 66 or 60000/1001, got %r" % text)
    num, den = int(match.group(1)), int(match.group(2) or 1)
    if num == 0 or den == 0:
        raise ValueError("must be a positive rate, got %r" % text)
    exact = Fraction(num, den)
    return exact.numerator, exact.denominator


def round_half_up(value):
    """Nearest integer to an exact value (int or Fraction); halves round up."""
    return math.floor(value + Fraction(1, 2))


def fraction_text(value):
    """Exact fraction as 'n/d', always with a denominator (2 is '2/1')."""
    return "%d/%d" % (value.numerator, value.denominator)


def speed_text(k):
    """Describe the frame-based speed change for rate ratio k (exact)."""
    if k > 1:
        return "%s times faster" % k
    if k < 1:
        return "%s times slower" % (1 / k)
    return "at the same speed"


def rescale_frames(frames, k, line_no):
    """Map one frame= list to the target rate (k = target rate / scene rate, exact).

    Two values (start, end inclusive): start' = round_half_up(s*k), end' = round_half_up((e+1)*k) - 1.
    If end' < start' the object is shorter than one output frame: end' = start' and shortened is True.

    More values: the first point uses the start rule, the last point the end rule, the middle points
    round_half_up(p*k). The result must be strictly increasing, otherwise the rewrite is refused.

    Returns (new_frames, shortened).
    """
    if len(frames) == 2:
        start, end = frames
        if end < start:
            raise Refusal(line_no, "frame= end %d is before start %d" % (end, start))
        new_start = round_half_up(start * k)
        new_end = round_half_up((end + 1) * k) - 1
        if new_end < new_start:
            return [new_start, new_start], True
        return [new_start, new_end], False
    if any(b <= a for a, b in zip(frames, frames[1:])):
        raise Refusal(line_no, "frame= intermediate points are not strictly increasing: %s"
                      % ",".join(str(x) for x in frames))
    result = ([round_half_up(frames[0] * k)]
              + [round_half_up(m * k) for m in frames[1:-1]]
              + [round_half_up((frames[-1] + 1) * k) - 1])
    if any(b <= a for a, b in zip(result, result[1:])):
        raise Refusal(line_no, "frame= points collide or stop increasing after rescaling to the target rate "
                      "(%s -> %s); refused rather than merging points"
                      % (",".join(str(x) for x in frames), ",".join(str(x) for x in result)))
    return result, False


def check_layers(object_frame_lines, object_layer, object_scene, frame_values, new_frames):
    """Objects on one layer of one scene keep their order and their touching, and never overlap after rescaling.

    Each scene has its own timeline, so objects are grouped by (scene, layer). Only pairs that do not overlap
    in the input are checked. Raises Refusal on the later object's frame= line.
    """
    groups = {}
    for obj_no in object_frame_lines:
        key = (object_scene.get(obj_no), object_layer.get(obj_no))
        groups.setdefault(key, []).append(obj_no)
    for objs in groups.values():
        objs.sort(key=lambda n: frame_values[n][0])
        for earlier, later in zip(objs, objs[1:]):
            old_end, old_start = frame_values[earlier][-1], frame_values[later][0]
            if old_end >= old_start:
                continue
            new_end, new_start = new_frames[earlier][-1], new_frames[later][0]
            line = object_frame_lines[later] + 1
            other = object_frame_lines[earlier] + 1
            if old_end + 1 == old_start:
                if new_end + 1 != new_start:
                    raise Refusal(line, "object touches the object at line %d on the same scene and layer, but "
                                  "after rescaling they no longer touch (one of them became 1 frame); refused"
                                  % other)
            elif new_end >= new_start:
                raise Refusal(line, "object overlaps the object at line %d on the same scene and layer after "
                              "rescaling; refused" % other)


def decimal_places(text):
    """Digits after the decimal point of a plain number: '-200.00' gives 2, '5' gives 0."""
    return len(text.partition(".")[2])


def round_half_away(value):
    """Nearest integer to an exact value; halves round away from zero."""
    magnitude = math.floor(abs(value) + Fraction(1, 2))
    return -magnitude if value < 0 else magnitude


def quantize(value, places):
    """Round an exact value half away from zero to `places` decimals. Returns (units, text).

    units counts steps of 10**-places; text has exactly `places` decimals and no sign for zero.
    """
    units = round_half_away(value * 10 ** places)
    digits = str(abs(units)).rjust(places + 1, "0")
    sign = "-" if units < 0 else ""
    if places == 0:
        return units, sign + digits
    return units, sign + digits[:-places] + "." + digits[-places:]


def segment_index(frames, t):
    """Index i of the segment [frames[i], frames[i+1]] that gives the value at frame t.

    Before the first point the first segment is used and after the last point the last one (extrapolation).
    """
    if t <= frames[0]:
        return 0
    if t >= frames[-1]:
        return len(frames) - 2
    return next(i for i in range(len(frames) - 1) if t <= frames[i + 1])


def curve_value(frames, values, t):
    """Exact value at frame t of the piecewise-linear curve through (frames[i], values[i])."""
    i = segment_index(frames, t)
    return values[i] + (values[i + 1] - values[i]) * (t - frames[i]) / (frames[i + 1] - frames[i])


def held_curve_value(frames, values, t):
    """Exact value at frame t of the curve through (frames[i], values[i]), held at the end values outside the keyframes.

    AviUtl keeps the last value after the last keyframe, and before the first frame the object does not exist (its start
    value is used).
    """
    return curve_value(frames, values, min(max(t, frames[0]), frames[-1]))


def precise_keyframes(frames, new, k):
    """Keyframes of a --precise object: the rescaled ones plus the points where the hold at either end starts.

    frames are the original keyframes, new the rescaled ones, k = Rt / Rp. New frame n shows held_curve_value(n / k).
    If the new last frame lies after the original last frame (e' / k > e), the value is linear up to m = floor(e * k)
    and e from m + 1 on: m and m + 1 become keyframes (only m when e * k is an integer). If the new first frame lies
    before the original first frame (s' / k < s), n0 = ceil(s * k) is the first frame on the curve: n0 becomes a
    keyframe (and n0 - 1 when that is after s'). Points are never merged, and an object whose new frames are the
    same two points (shortened to one frame) gets none. Returns the sorted list (rescaled points keep their positions).
    """
    s, e = frames[0], frames[-1]
    s2, e2 = new[0], new[-1]
    extra = set()
    if Fraction(e2) / k > e:
        m = min(max(math.floor(e * k), s2), e2)
        if m < e2:
            extra.add(m)
            if (e * k).denominator != 1:
                extra.add(m + 1)
    if Fraction(s2) / k < s:
        n0 = math.ceil(s * k)
        if n0 <= e2:
            extra.add(n0)
            if n0 - 1 > s2:
                extra.add(n0 - 1)
    return sorted(list(new) + [n for n in extra if n not in new])


def precise_track_problem(line_no, mode, param, values):
    """Reason a trackbar blocks --precise for its object, or None when the track can be resampled exactly."""
    if "|" in param:
        return "trackbar at line %d has a '|' payload" % line_no
    if mode == PRECISE_LINEAR_MODE and param == PRECISE_LINEAR_PARAM:
        return None
    if len(set(values)) == 1:
        return None
    return ("trackbar at line %d uses mode %s with parameter %s and changing values; only %s,%s or constant "
            "values can be resampled exactly" % (line_no, mode, param, PRECISE_LINEAR_MODE, PRECISE_LINEAR_PARAM))


def precise_resample(lines, effect_lines, section_of, object_frame_lines, frame_values, new_frames, k_of, notes,
                     extra_decimals=0, skip_objects=frozenset()):
    """Resample trackbar values at the new keyframes from the exact original curve (--precise).

    An object is resampled only when every trackbar of it (except PRECISE_EXEMPT_MODES) passes
    precise_track_problem, and its frame= list is not two equal points (PRECISE_ZERO_LENGTH_REASON);
    otherwise it keeps its old values and is listed as skipped. An object in skip_objects (one that holds a time
    control effect, whose frame= list must stay as it is) is left alone and is not listed.
    The new keyframes are precise_keyframes(); every trackbar of the object gets one value per keyframe, taken from the
    original curve held at its end values (held_curve_value). A resampled track is written with its own decimal count d
    plus extra_decimals.
    object_frame_lines holds only the objects of followed scenes; k_of maps each of them to the rate ratio of its
    scene.
    Returns (bodies, report, frames_out): bodies maps a trackbar line index to its rewritten text, report is the
    "precise" object of the JSON (it has "extra_decimals" only when extra_decimals > 0), frames_out maps an object
    number to its new frame list when that has more points than the rescaled one.
    Tracks above PRECISE_NOTE_RATIO add a note (not a refusal).
    """
    tracks = {}
    for idx in effect_lines:
        value = lines[idx].partition("=")[2]
        if not is_trackbar(value) or value.split(",")[-2] in PRECISE_EXEMPT_MODES:
            continue
        tracks.setdefault(section_of[idx][1], []).append(idx)

    bodies = {}
    frames_out = {}
    skipped = []
    track_reports = []
    applied = 0
    for obj_no, frame_idx in object_frame_lines.items():
        if obj_no not in tracks or obj_no in skip_objects:
            continue
        frames, new, k = frame_values[obj_no], new_frames[obj_no], k_of[obj_no]
        problem = None
        parsed = []
        for idx in tracks[obj_no]:
            key, _, value = lines[idx].partition("=")
            parts = value.split(",")
            values = [Fraction(x) for x in parts[:-2]]
            problem = precise_track_problem(idx + 1, parts[-2], parts[-1], values)
            if problem:
                break
            if len(frames) == 2 and frames[0] == frames[1]:
                problem = PRECISE_ZERO_LENGTH_REASON
                break
            parsed.append((idx, key, parts, values))
        if problem:
            skipped.append({"line": frame_idx + 1, "reason": problem})
            continue
        applied += 1
        points = precise_keyframes(frames, new, k)
        if points != list(new):
            frames_out[obj_no] = points
        for idx, key, parts, values in parsed:
            places = max(decimal_places(x) for x in parts[:-2]) + extra_decimals
            scale = 10 ** places
            exact = [held_curve_value(frames, values, Fraction(f) / k) for f in points]
            quantized = [quantize(x, places) for x in exact]
            error = max(abs(Fraction(units, scale) - x) for (units, _), x in zip(quantized, exact))
            span = max(values) - min(values)
            ratio = error / span if span else Fraction(0)
            end_shift = Fraction(quantized[-1][0], scale) - values[-1]
            bodies[idx] = "%s=%s" % (key, ",".join([text for _, text in quantized] + parts[-2:]))
            track_reports.append({
                "object_line": frame_idx + 1,
                "line": idx + 1,
                "max_quant_error": quantize(error, 6)[1],
                "max_quant_error_pct": quantize(ratio * 100, 6)[1],
                "end_extrapolation": quantize(end_shift, 6)[1],
            })
            if ratio > PRECISE_NOTE_RATIO:
                notes.append("precise: trackbar at line %d (object at line %d) quantization error is %s%% of its "
                             "range, above 0.01%%" % (idx + 1, frame_idx + 1, quantize(ratio * 100, 6)[1]))
    report = {"enabled": True}
    if extra_decimals:
        report["extra_decimals"] = extra_decimals
    report.update({"applied": applied, "skipped": skipped, "tracks": track_reports})
    return bodies, report, frames_out


def parse_time_control_position(value):
    """Parse the value of a time control 位置 line: 'a,b,直線移動(時間制御),flag'.

    Returns (a_text, b_text, flag_text), or None when it is not exactly two plain numbers, the time control mode
    and a flag without '|' payload.
    """
    parts = value.split(",")
    if len(parts) != 4 or not RE_NUMBER.match(parts[0]) or not RE_NUMBER.match(parts[1]):
        return None
    if parts[2] != TIME_CONTROL_MODE or "|" in parts[3]:
        return None
    return parts[0], parts[1], parts[3]


def clamp_percent(value):
    """The host clamps the end values of a time control 位置 to 0..100."""
    return min(max(value, Fraction(0)), Fraction(100))


def time_control_tau(old, a, b, drop, f):
    """Timeline frame shown by the controlled layers of the original time control at the (real) frame f.

    old = (s, e) are the first and last frame of the time control object, L = e - s + 1, a and b the 位置 values and
    drop the コマ落ち K. The end values are clamped first (a_c, b_c), p(f) = a_c + (b_c - a_c) * (f - s) / (L - 1)
    (p = a_c when L = 1), tau = s + p * (L - 1) / 100. f is held at s and e (the object does not exist before s, and
    after e the last value stays). With K >= 2 the layers show the state of the first frame of the segment,
    g = s + floor((f - s) / K) * K.
    """
    s, e = old
    length = e - s + 1
    f = min(max(Fraction(f), s), e)
    if drop > 1:
        f = s + ((f - s) // drop) * drop
    a_c, b_c = clamp_percent(a), clamp_percent(b)
    p = a_c if length == 1 else a_c + (b_c - a_c) * (f - s) / (length - 1)
    return s + p * (length - 1) / 100


def retime_time_control(old, new, a, b, k):
    """Exact two-point 位置 values (a', b') of a time control object after retiming. a and b are Fractions.

    old = (s, e) and new = (s', e') are the first and last frame before and after; k = Rt / Rp.
    With L = e - s + 1, the clamped end values a_c, b_c and p(f) = a_c + (b_c - a_c) * (f - s) / (L - 1) (p = a_c when
    L = 1), the original controlled layers show the timeline frame tau(f) = s + p(f) / 100 * (L - 1), continued
    linearly outside s..e here. The new project must show the same content at the same time: at new frame n (original
    frame n / k) the shown new frame is k * tau(n / k), and s' + p'(n) / 100 * (L' - 1) must equal it. p'(n) is linear
    in n, so only n = s' and n = e' are needed. When L' = 1 the shown frame is s' whatever p' is; both values are then
    p(s' / k).
    The results are not clamped; a two-point line is exact only when both are in 0..100.
    """
    s, e = old
    s2, e2 = new
    length, length2 = e - s + 1, e2 - s2 + 1
    a_c, b_c = clamp_percent(a), clamp_percent(b)

    def percent(frame):
        return a_c if length == 1 else a_c + (b_c - a_c) * (frame - s) / (length - 1)

    def shown(n):
        frame = Fraction(n) / k
        tau = Fraction(s) if length == 1 else s + percent(frame) / 100 * (length - 1)
        return k * tau

    if length2 == 1:
        p = percent(Fraction(s2) / k)
        return p, p
    return (100 * (shown(s2) - s2) / (length2 - 1), 100 * (shown(e2) - s2) / (length2 - 1))


def time_control_steps(old, new, a, b, drop, k, places):
    """Staircase 位置 of a time control object after retiming, for the cases a two-point line cannot give.

    For every new frame n of s'..e' the original shows tau(g(f)) at f = n / k held to s..e (time_control_tau), which is
    k * tau new frames; so p'(n) = 100 * (k * tau - s') / (L' - 1), clamped to 0..100 and rounded half away from zero
    to `places` decimals. The points are s', every frame whose rounded value differs from the frame before, and e'
    (when it is not one already); the value of a point is the p'(n) of its frame. Requires L' >= 2.
    Returns (points, value texts, last_differs): last_differs is True when the value at e' differs from the one at
    e' - 1, which the host does not show (the last point is never shown). Returns None when more than
    TIME_CONTROL_MAX_POINTS points are needed.
    """
    s2, e2 = new
    length2 = e2 - s2 + 1
    points, texts = [], []
    previous = before_last = None  # rounded value of the frame before n, and of frame e' - 1 when the loop ends
    for n in range(s2, e2 + 1):
        tau = time_control_tau(old, a, b, drop, Fraction(n) / k)
        units, text = quantize(clamp_percent(100 * (k * tau - s2) / (length2 - 1)), places)
        if units != previous:
            points.append(n)
            texts.append(text)
            if len(points) > TIME_CONTROL_MAX_POINTS:
                return None
        before_last, previous = previous, units
    if points[-1] != e2:
        points.append(e2)
        texts.append(text)
        if len(points) > TIME_CONTROL_MAX_POINTS:
            return None
    return points, texts, previous != before_last


def unescape_newlines(text):
    """A .aup2 text value may hold newlines as literal backslash escapes; turn them into real newlines."""
    return text.replace("\\r\\n", "\n").replace("\\r", "\n").replace("\\n", "\n")


def script_text(effect_name, key, value):
    """Return the part of one effect value that is scanned for script markers ('' when none)."""
    if effect_name == TEXT_EFFECT_NAME:
        if key != TEXT_EFFECT_NAME:
            return ""
        return "\n".join(RE_TEXT_SCRIPT.findall(unescape_newlines(value)))
    return value


def convert(data, out_path, factor=None, target=None, allow_frame_scripts=False, precise=False,
            precise_extra_decimals=0, subscene_rate="auto"):
    """Return (output_bytes, changes, notes, warnings, summary).

    Give exactly one of:
      factor: integer K >= 2; the target rate is the exported scene's rate times K (video.scale is kept);
      target: (numerator, denominator) of the exact output rate, as returned by parse_target_fps.

    The exported scene is the [project] display.scene (default 0). Other scenes follow the target rate or are kept
    byte for byte according to subscene_rate (see SUBSCENE_RATE_POLICIES); every followed scene is converted with its
    own ratio k = target rate / scene rate.

    Raises Refusal on any unsupported content. Frame-dependent script is refused unless
    allow_frame_scripts is set; then each occurrence is returned in warnings and notes.
    precise resamples trackbar values at the new keyframes (see precise_resample); summary then has "precise".
    A 時間制御(オブジェクト) effect of a followed scene gets a new 位置 (a two-point line from retime_time_control when that
    is exact, else a staircase from time_control_steps) and, for コマ落ち K >= 2, K x k with the line or 1 with the
    staircase; it is kept out of precise_resample, and so is every object that holds one.
    """
    if (factor is None) == (target is None):
        raise ValueError("convert() needs exactly one of factor or target")
    if factor is not None and factor < 2:
        raise Refusal(None, "factor must be an integer >= 2, got %d" % factor)
    if subscene_rate not in SUBSCENE_RATE_POLICIES:
        raise ValueError("subscene_rate must be one of %s, got %r" % (", ".join(SUBSCENE_RATE_POLICIES), subscene_rate))

    has_bom = data.startswith(UTF8_BOM)
    body_bytes = data[len(UTF8_BOM):] if has_bom else data
    try:
        text = body_bytes.decode("utf-8")
    except UnicodeDecodeError as exc:
        raise Refusal(None, "input is not valid UTF-8: %s" % exc)

    records = split_records(text)
    lines = [body for body, _ in records]
    notes = []
    warnings = []

    # Pass 1: map each line to its section.
    section = None
    section_of = []
    project_file_lines = []
    scene_lines = {}
    object_frame_lines = {}
    object_layer = {}
    object_scene_lines = {}  # obj_no -> (value, line_index) from its scene= line
    obj_header_scene = {}  # obj_no -> number of the [scene.N] header above its [N] header (None if none)
    header_scene = None
    effect_lines = []
    effect_props = {}  # (obj_no, effect_no) -> {key: (value, line_index)}
    project_seen = False
    display_scene = None  # (value, line_index) of the first [project] display.scene= line
    frame_drop_lines = []  # (line_index, value, section) of every コマ落ち line
    for idx, body in enumerate(lines):
        if body.startswith("[") and body.endswith("]"):
            if body == "[project]":
                project_seen = True
            m_scene = RE_SCENE_HDR.match(body)
            m_obj = RE_OBJECT_HDR.match(body)
            m_eff = RE_EFFECT_HDR.match(body)
            if m_scene:
                scene_lines.setdefault(int(m_scene.group(1)), [])
                section = ("scene", int(m_scene.group(1)))
                header_scene = int(m_scene.group(1))
            elif m_obj:
                section = ("object", int(m_obj.group(1)))
                obj_header_scene[int(m_obj.group(1))] = header_scene
            elif m_eff:
                section = ("effect", int(m_eff.group(1)), int(m_eff.group(2)))
            elif body == "[project]":
                section = ("project",)
            else:
                section = ("other",)
            section_of.append(section)
            continue
        section_of.append(section)
        if section is None or "=" not in body:
            continue
        key, _, value = body.partition("=")
        kind = section[0]
        if kind == "project" and key == "file":
            project_file_lines.append(idx)
        elif kind == "project" and key == "display.scene":
            if display_scene is None:
                display_scene = (value, idx)
        elif kind == "scene":
            scene_lines[section[1]].append(idx)
        elif kind == "object" and key == "frame":
            if section[1] in object_frame_lines:
                raise Refusal(idx + 1, "duplicate frame= in object [%d]" % section[1])
            object_frame_lines[section[1]] = idx
        elif kind == "object" and key == "layer":
            object_layer[section[1]] = value
        elif kind == "object" and key == "scene":
            object_scene_lines[section[1]] = (value, idx)
        elif kind == "effect":
            effect_lines.append(idx)
            effect_props.setdefault((section[1], section[2]), {})[key] = (value, idx)

        if key == KEY_FRAME_DROP:
            frame_drop_lines.append((idx, value, section))

    if not project_seen or len(project_file_lines) != 1:
        raise Refusal(None, "expected exactly one 'file=' line in [project]")
    if not scene_lines:
        raise Refusal(None, "no [scene.N] section found")

    # Each object belongs to one scene: its scene= line if it has one, else the [scene.N] section above it.
    object_scene = {}
    for obj_no, header in obj_header_scene.items():
        if obj_no in object_scene_lines:
            value, idx = object_scene_lines[obj_no]
            if not RE_INT.match(value):
                raise Refusal(idx + 1, "object scene= must be an integer, got %r" % value)
            object_scene[obj_no] = int(value)
        else:
            object_scene[obj_no] = 0 if header is None else header

    # Pass 2: scene checks and the plan of each scene (follow the target rate or keep its own).
    scene_rate_lines = {}  # scene_no -> (video.rate line index, video.scale line index or None)
    scene_rates = {}  # scene_no -> (video.rate, video.scale)
    scene_names = {}  # scene_no -> value of the first name= line ('' when there is none)
    for scene_no, idxs in sorted(scene_lines.items()):
        values = {}
        scene_names[scene_no] = ""
        name_seen = False
        for idx in idxs:
            key, _, value = lines[idx].partition("=")
            if key in ("video.rate", "video.scale"):
                if not RE_INT.match(value) or int(value) == 0:
                    raise Refusal(idx + 1, "%s must be a positive integer, got %r" % (key, value))
                values[key] = (int(value), idx)
            elif key in SCENE_KNOWN_KEYS:
                if key in SCENE_FRAME_KEYS and not RE_INT.match(value):
                    raise Refusal(idx + 1, "%s must be an integer, got %r" % (key, value))
                if key == "name" and not name_seen:
                    scene_names[scene_no] = value
                    name_seen = True
            elif any(marker in key for marker in SCENE_UNKNOWN_REFUSE_MARKERS):
                raise Refusal(idx + 1, "unknown scene key %r may hold frame/mark/range data" % key)
        if "video.rate" not in values:
            raise Refusal(None, "[scene.%d] has no video.rate" % scene_no)
        scale_value, scale_idx = values.get("video.scale", (1, None))
        scene_rate_lines[scene_no] = (values["video.rate"][1], scale_idx)
        scene_rates[scene_no] = (values["video.rate"][0], scale_value)

    # The exported scene is [project] display.scene (0 when absent); its rate sets the target in factor mode.
    exported = 0
    if display_scene is not None:
        if not RE_INT.match(display_scene[0]):
            raise Refusal(display_scene[1] + 1, "display.scene must be a non-negative integer, got %r"
                          % display_scene[0])
        exported = int(display_scene[0])
    if exported not in scene_rates:
        raise Refusal(None, "display.scene %d does not exist" % exported)
    src_rate, src_scale = scene_rates[exported]
    if factor is not None:
        target_num, target_den = src_rate * factor, src_scale
    else:
        target_num, target_den = target
    rate_dst = Fraction(target_num, target_den)
    rate_exported = Fraction(src_rate, src_scale)

    scene_plan = {}
    for scene_no in sorted(scene_rates):
        rate_i = Fraction(*scene_rates[scene_no])
        if scene_no == exported or subscene_rate == "follow":
            follow = True
        elif subscene_rate == "keep":
            follow = False
        else:
            follow = rate_i >= rate_exported
        scene_plan[scene_no] = ScenePlan(scene_no, scene_names[scene_no], rate_i, follow,
                                         rate_dst / rate_i if follow else Fraction(1), scene_no == exported)
    for scene_no, plan in scene_plan.items():
        if not plan.follow:
            notes.append("scene %d kept at its own rate %s" % (scene_no, fraction_text(plan.rate_src)))
        elif scene_rate_lines[scene_no][1] is None:
            notes.append("[scene.%d] has no video.scale; read as 1 and written after video.rate" % scene_no)

    # An object whose scene has no [scene.N] section is converted like the exported scene.
    def plan_of(obj_no):
        return scene_plan.get(object_scene.get(obj_no), scene_plan[exported])

    k = scene_plan[exported].k
    rate_src = rate_exported

    # コマ落ち: 1 stays. K >= 2 is accepted in a time control effect (rewritten with it below); any other value, and
    # K >= 2 elsewhere, is refused. Lines of kept scenes are not looked at; lines outside objects, effects and scenes are.
    def is_time_control(obj_no, effect_no):
        return effect_props.get((obj_no, effect_no), {}).get("effect.name", (None, None))[0] == TIME_CONTROL_EFFECT_NAME

    for idx, value, sec in frame_drop_lines:
        if sec[0] == "scene":
            plan = scene_plan[sec[1]]
        elif sec[0] in ("object", "effect"):
            plan = plan_of(sec[1])
        else:
            plan = scene_plan[exported]
        if not plan.follow:
            continue
        if not RE_INT.match(value) or int(value) == 0:
            raise Refusal(idx + 1, "%s=%s (frame drop) must be a positive integer" % (KEY_FRAME_DROP, value))
        if int(value) != 1 and (sec[0] != "effect" or not is_time_control(sec[1], sec[2])):
            raise Refusal(idx + 1, "%s=%s (frame drop) is only converted inside a %s effect"
                          % (KEY_FRAME_DROP, value, TIME_CONTROL_EFFECT_NAME))

    # Pass 3: object frame lists, and trackbars that depend on them. Objects of kept scenes are not looked at.
    followed_frame_lines = {obj_no: idx for obj_no, idx in object_frame_lines.items() if plan_of(obj_no).follow}
    object_k = {obj_no: plan_of(obj_no).k for obj_no in followed_frame_lines}
    frame_values = {}
    for obj_no, idx in followed_frame_lines.items():
        frame_values[obj_no] = parse_frames(lines[idx].partition("=")[2], idx + 1)
    counter_lines = []
    for (obj_no, eff_no), props in sorted(effect_props.items()):
        if props.get("effect.name", (None, None))[0] != COUNTER_EFFECT_NAME or not plan_of(obj_no).follow:
            continue
        name_idx = props["effect.name"][1]
        if "速度" not in props:
            raise Refusal(name_idx + 1, "%s has no 速度= line" % COUNTER_EFFECT_NAME)
        speed, speed_idx = props["速度"]
        if not RE_NUMBER.match(speed):
            raise Refusal(speed_idx + 1, "%s 速度 must be a plain number, got %r" % (COUNTER_EFFECT_NAME, speed))
        counter_lines.append(name_idx + 1)
    if counter_lines:
        notes.append("%s: time-based (速度 = counts per second), passed through unchanged (lines %s)"
                     % (COUNTER_EFFECT_NAME, ", ".join(str(n) for n in counter_lines)))

    # 時間制御(オブジェクト) of followed scenes: 位置 must be a two-point track on an object with two frame= points.
    time_controls = []  # (obj_no, effect_no, position line index, (a, b, flag) texts)
    for (obj_no, eff_no), props in sorted(effect_props.items()):
        if not is_time_control(obj_no, eff_no) or not plan_of(obj_no).follow:
            continue
        pos_value, pos_idx = props.get(TIME_CONTROL_KEY_POSITION, (None, props["effect.name"][1]))
        position = parse_time_control_position(pos_value) if pos_value is not None else None
        if position is None or len(frame_values.get(obj_no, ())) != 2:
            raise Refusal(pos_idx + 1, TIME_CONTROL_POSITION_REASON)
        time_controls.append((obj_no, eff_no, pos_idx, position))

    time_controls.sort(key=lambda item: item[2])  # file order, whatever the object numbers are
    time_control_positions = {pos_idx for _, _, pos_idx, _ in time_controls}
    for idx in effect_lines:
        section = section_of[idx]
        if not plan_of(section[1]).follow:
            continue
        key, _, value = lines[idx].partition("=")
        props = effect_props[(section[1], section[2])]
        effect_name = props.get("effect.name", (None, None))[0]
        if key == "effect.name" and value in REFUSED_EFFECT_NAMES:
            raise Refusal(idx + 1, "effect %r is not supported" % value)
        for marker in RE_FRAME_MARKER.finditer(script_text(effect_name, key, value)):
            where = "%s effect (%s)" % (effect_name, key)
            if not allow_frame_scripts:
                raise Refusal(idx + 1, "script text uses frame-dependent marker %r in %s; "
                              "refused (pass --allow-frame-scripts to convert anyway)"
                              % (marker.group(0), where))
            text = ("WARNING: line %d: frame-dependent script marker %r in %s: frame-based animation "
                    "will run %s and per-frame random sequences change"
                    % (idx + 1, marker.group(0), where, speed_text(plan_of(section[1]).k)))
            warnings.append(text)
            notes.append(text)
        if idx in time_control_positions:
            continue
        if not is_trackbar(value):
            continue
        parts = value.split(",")
        mode, param = parts[-2], parts[-1]
        if mode not in TRACK_MODES:
            raise Refusal(idx + 1, "trackbar mode %r is not on the whitelist" % mode)
        if "|" in param:
            payload = param.split("|", 1)[1]
            if mode not in TRACK_PAYLOAD_MODES and payload not in TRACK_PIPE_PAYLOADS:
                raise Refusal(idx + 1, "trackbar '|' payload %r is not on the whitelist" % payload)
        obj_no = section[1]
        if obj_no in frame_values and mode not in TRACK_COUNT_EXEMPT_MODES:
            count = len(parts) - 2
            if count != len(frame_values[obj_no]):
                raise Refusal(idx + 1, "trackbar has %d values but object [%d] has %d frame points"
                              % (count, obj_no, len(frame_values[obj_no])))

    new_frames = {}
    shortened = set()
    for obj_no, idx in followed_frame_lines.items():
        new_frames[obj_no], was_short = rescale_frames(frame_values[obj_no], object_k[obj_no], idx + 1)
        if was_short:
            shortened.add(idx + 1)
            notes.append("object at line %d shortened to 1 frame" % (idx + 1))
    check_layers(followed_frame_lines, object_layer, object_scene, frame_values, new_frames)

    # 時間制御(オブジェクト): 位置 is recomputed exactly (always, with or without --precise). A two-point line is kept when it is
    # exact (K = 1, or K x k and s x k are integers; and both new values are in 0..100), with コマ落ち K x k. Otherwise
    # the object gets a 瞬間移動 staircase with one point per change and コマ落ち 1. A scene with k = 1 is not touched.
    time_control_bodies = {}  # line index -> rewritten 位置, コマ落ち or constant trackbar line
    final_frames = dict(new_frames)  # object number -> frame list to write (more points than new_frames: see below)
    for obj_no, eff_no, pos_idx, (a_text, b_text, flag) in time_controls:
        k_obj = object_k[obj_no]
        if k_obj == 1:
            continue
        frame_idx = followed_frame_lines[obj_no]
        old_frames, new = frame_values[obj_no], new_frames[obj_no]
        old_range, new_range = (old_frames[0], old_frames[-1]), (new[0], new[-1])
        a, b = Fraction(a_text), Fraction(b_text)
        places = max(decimal_places(a_text), decimal_places(b_text))
        props = effect_props[(obj_no, eff_no)]
        drop_text, drop_idx = props.get(KEY_FRAME_DROP, ("1", None))
        drop = int(drop_text)
        length2 = new_range[1] - new_range[0] + 1
        drop_k = drop * k_obj
        new_a, new_b = retime_time_control(old_range, new_range, a, b, k_obj)
        if length2 == 1:
            # One frame: the layers show s' whatever 位置 is, and there is nothing to drop (1 unless K x k is an integer).
            keep_line = True
            new_drop = int(drop_k) if drop > 1 and drop_k.denominator == 1 and drop_k >= 1 else 1
        else:
            keep_line = ((drop == 1 or (drop_k.denominator == 1 and (old_range[0] * k_obj).denominator == 1))
                         and 0 <= new_a <= 100 and 0 <= new_b <= 100)
            new_drop = drop if drop == 1 else int(drop_k)
        key = lines[pos_idx].partition("=")[0]
        if keep_line:
            new_a_text, new_b_text = quantize(new_a, places)[1], quantize(new_b, places)[1]
            body = "%s=%s,%s,%s,%s" % (key, new_a_text, new_b_text, TIME_CONTROL_MODE, flag)
            note = "time control at line %d: %s %s,%s -> %s,%s" % (
                frame_idx + 1, TIME_CONTROL_KEY_POSITION, a_text, b_text, new_a_text, new_b_text)
            changed = body != lines[pos_idx]
            if changed:
                time_control_bodies[pos_idx] = body
            if new_drop != drop:
                note += "; %s %d -> %d" % (KEY_FRAME_DROP, drop, new_drop)
                time_control_bodies[drop_idx] = "%s=%d" % (KEY_FRAME_DROP, new_drop)
                changed = True
            if changed:
                notes.append(note)
            continue

        # Staircase. The frame= list gets more points, so every other trackbar of the object must be constant (its
        # value is repeated); a 再生範囲 trackbar has no per-point values and stays.
        repeated = {}
        for idx in effect_lines:
            if section_of[idx][1] != obj_no or idx == pos_idx or not is_trackbar(lines[idx].partition("=")[2]):
                continue
            key_other, _, value = lines[idx].partition("=")
            parts = value.split(",")
            if parts[-2] in PRECISE_EXEMPT_MODES:
                continue
            if len({Fraction(x) for x in parts[:-2]}) != 1:
                raise Refusal(None, "time control at line %d: the object has other animated trackbars" % (pos_idx + 1))
            repeated[idx] = (key_other, parts)
        steps = time_control_steps(old_range, new_range, a, b, drop, k_obj, places)
        if steps is None:
            raise Refusal(None, "time control at line %d needs more than %d steps"
                          % (pos_idx + 1, TIME_CONTROL_MAX_POINTS))
        points, texts, last_differs = steps
        final_frames[obj_no] = points
        time_control_bodies[pos_idx] = "%s=%s,%s,%s" % (key, ",".join(texts), TIME_CONTROL_STEP_MODE,
                                                         TIME_CONTROL_STEP_FLAG)
        for idx, (key_other, parts) in repeated.items():
            time_control_bodies[idx] = "%s=%s" % (key_other, ",".join([parts[0]] * len(points) + parts[-2:]))
        note = "time control at line %d: %s %s,%s -> %d steps (%s)" % (
            frame_idx + 1, TIME_CONTROL_KEY_POSITION, a_text, b_text, len(points), TIME_CONTROL_STEP_MODE)
        if drop > 1:
            note += "; %s %d -> 1" % (KEY_FRAME_DROP, drop)
            time_control_bodies[drop_idx] = "%s=1" % KEY_FRAME_DROP
        if last_differs:
            note += "; " + TIME_CONTROL_LAST_FRAME_NOTE
        notes.append(note)

    precise_bodies, precise_report = {}, None
    if precise:
        time_control_objects = {obj_no for obj_no, _, _, _ in time_controls}
        precise_bodies, precise_report, precise_frames = precise_resample(
            lines, [idx for idx in effect_lines
                    if plan_of(section_of[idx][1]).follow and not is_time_control(*section_of[idx][1:])],
            section_of, followed_frame_lines, frame_values, new_frames, object_k, notes,
            extra_decimals=precise_extra_decimals, skip_objects=time_control_objects)
        final_frames.update(precise_frames)

    # Pass 4: build the rewritten lines.
    project_idx = project_file_lines[0]
    new_bodies = list(lines)
    changes = []
    inserts = {}  # line index -> bodies to write right after that line

    def set_line(idx, new_body):
        if new_body != lines[idx]:
            changes.append({"line": idx + 1, "old": lines[idx], "new": new_body})
            new_bodies[idx] = new_body

    set_line(project_idx, "file=" + os.path.abspath(out_path))

    for scene_no, idxs in scene_lines.items():
        if not scene_plan[scene_no].follow:
            continue
        scene_k = scene_plan[scene_no].k
        rate_idx, scale_idx = scene_rate_lines[scene_no]
        for idx in idxs:
            key, _, value = lines[idx].partition("=")
            if idx == rate_idx:
                set_line(idx, "video.rate=%d" % target_num)
                if scale_idx is None:
                    inserted = "video.scale=%d" % target_den
                    inserts.setdefault(idx, []).append(inserted)
                    changes.append({"line": idx + 1, "old": None, "new": inserted, "inserted": True})
            elif idx == scale_idx:
                if int(value) != target_den:
                    set_line(idx, "video.scale=%d" % target_den)
            elif key in SCENE_FRAME_KEYS:
                set_line(idx, "%s=%d" % (key, round_half_up(int(value) * scene_k)))

    timing_objects = []
    for obj_no, idx in followed_frame_lines.items():
        old, new = frame_values[obj_no], new_frames[obj_no]
        scene_src = plan_of(obj_no).rate_src
        set_line(idx, "frame=" + ",".join(str(x) for x in final_frames[obj_no]))
        timing_objects.append({
            "line": idx + 1,
            "start_error_s": str(abs(Fraction(new[0]) / rate_dst - Fraction(old[0]) / scene_src)),
            "end_error_s": str(abs(Fraction(new[-1] + 1) / rate_dst - Fraction(old[-1] + 1) / scene_src)),
            "shortened": idx + 1 in shortened,
        })
    for idx in sorted(precise_bodies):
        set_line(idx, precise_bodies[idx])
    for idx in sorted(time_control_bodies):
        set_line(idx, time_control_bodies[idx])
    start_errors = [Fraction(o["start_error_s"]) for o in timing_objects]
    end_errors = [Fraction(o["end_error_s"]) for o in timing_objects]

    out_bytes = bytearray()
    if has_bom:
        out_bytes += UTF8_BOM
    for idx, ((body, eol), new_body) in enumerate(zip(records, new_bodies)):
        extra = inserts.get(idx, [])
        if extra and not eol:
            eol = "\n"
        out_bytes += (new_body + eol).encode("utf-8")
        for text in extra:
            out_bytes += (text + eol).encode("utf-8")

    summary = {
        "source_rate": fraction_text(rate_src),
        "target_rate": fraction_text(rate_dst),
        "k": fraction_text(k),
        "subscene_rate": subscene_rate,
        "scenes": [{
            "scene": plan.scene,
            "name": plan.name,
            "source_rate": fraction_text(plan.rate_src),
            "target_rate": fraction_text(rate_dst if plan.follow else plan.rate_src),
            "k": fraction_text(plan.k),
            "policy": "follow" if plan.follow else "keep",
            "exported": plan.exported,
        } for plan in scene_plan.values()],
        "timing": {
            "max_start_error_s": str(max(start_errors, default=Fraction(0))),
            "max_end_error_s": str(max(end_errors, default=Fraction(0))),
            "objects": timing_objects,
        },
    }
    if precise_report is not None:
        summary["precise"] = precise_report
    return bytes(out_bytes), changes, notes, warnings, summary


def same_path(a, b):
    if os.path.normcase(os.path.abspath(a)) == os.path.normcase(os.path.abspath(b)):
        return True
    return os.path.exists(a) and os.path.exists(b) and os.path.samefile(a, b)


def build_parser():
    parser = argparse.ArgumentParser(
        description="Retime an AviUtl2 .aup2 project to a new scene frame rate (integer factor or exact target rate).")
    parser.add_argument("--in", dest="in_path", required=True, help="input .aup2 (never modified)")
    parser.add_argument("--out", dest="out_path", required=True, help="new .aup2 to write")
    rate = parser.add_mutually_exclusive_group(required=True)
    rate.add_argument("--factor", dest="factor", type=int,
                      help="integer multiplier K >= 2 (target rate = scene rate * K)")
    rate.add_argument("--target-fps", dest="target_fps", metavar="RATE",
                      help="exact target rate: an integer or a fraction such as 66 or 60000/1001 "
                           "(decimals such as 59.94 are refused)")
    parser.add_argument("--subscene-rate", dest="subscene_rate", choices=SUBSCENE_RATE_POLICIES, default="auto",
                        help="rate of the scenes other than the exported one ([project] display.scene): auto "
                             "(follow when their rate is >= the exported scene's, else keep), follow (all scenes "
                             "get the target rate) or keep (they stay as they are). Default auto")
    parser.add_argument("--force", action="store_true", help="overwrite existing outputs")
    parser.add_argument("--allow-frame-scripts", action="store_true",
                        help="convert despite frame-dependent script (obj.frame, rand(...)); prints a WARNING per occurrence")
    parser.add_argument("--precise", action="store_true",
                        help="resample trackbar values at the new keyframes from the exact original curve; "
                             "adds a 'precise' section to the JSON")
    parser.add_argument("--precise-extra-decimals", dest="precise_extra_decimals", type=int, default=0,
                        metavar="N", help="with --precise: write resampled values with N more decimals than the "
                                          "original (integer 0 to 4, default 0)")
    return parser


def main(argv=None):
    parser = build_parser()
    args = parser.parse_args(argv)

    def refuse(message):
        say(message, err=True)
        return 2

    target = None
    if args.factor is not None:
        if args.factor < 2:
            return refuse("REFUSED: --factor must be an integer >= 2, got %d" % args.factor)
    else:
        try:
            target = parse_target_fps(args.target_fps)
        except ValueError as exc:
            return refuse("REFUSED: --target-fps %s" % exc)
    if not 0 <= args.precise_extra_decimals <= 4:
        return refuse("REFUSED: --precise-extra-decimals must be an integer from 0 to 4, got %d"
                      % args.precise_extra_decimals)
    if args.precise_extra_decimals and not args.precise:
        return refuse("REFUSED: --precise-extra-decimals needs --precise")
    if not os.path.isfile(args.in_path):
        return refuse("REFUSED: input not found: %s" % args.in_path)
    if same_path(args.in_path, args.out_path):
        return refuse("REFUSED: --out must differ from --in")
    json_path = args.out_path + SUFFIX_JSON
    if not args.force:
        for path in (args.out_path, json_path):
            if os.path.exists(path):
                return refuse("REFUSED: %s exists (use --force to overwrite)" % path)
    out_dir = os.path.dirname(os.path.abspath(args.out_path))
    if not os.path.isdir(out_dir):
        return refuse("REFUSED: output directory does not exist: %s" % out_dir)

    with open(args.in_path, "rb") as fh:
        in_data = fh.read()

    try:
        out_data, changes, notes, warnings, summary = convert(
            in_data, args.out_path, factor=args.factor, target=target,
            allow_frame_scripts=args.allow_frame_scripts, precise=args.precise,
            precise_extra_decimals=args.precise_extra_decimals, subscene_rate=args.subscene_rate)
    except Refusal as exc:
        return refuse(exc.message())

    for text in warnings:
        say(text, err=True)
    with open(args.out_path, "wb") as fh:
        fh.write(out_data)

    report = {
        "tool": "aup2_fps_upconvert",
        "factor": args.factor,
        "source_rate": summary["source_rate"],
        "target_rate": summary["target_rate"],
        "k": summary["k"],
        "subscene_rate": summary["subscene_rate"],
        "scenes": summary["scenes"],
        "input": {"path": os.path.abspath(args.in_path), "sha256": sha256_hex(in_data)},
        "output": {"path": os.path.abspath(args.out_path), "sha256": sha256_hex(out_data)},
        "changed_line_count": len(changes),
        "changes": changes,
        "timing": summary["timing"],
        "notes": notes,
    }
    if "precise" in summary:
        report["precise"] = summary["precise"]
    with open(json_path, "w", encoding="utf-8", newline="\n") as fh:
        json.dump(report, fh, ensure_ascii=False, indent=2)
        fh.write("\n")

    say("wrote %s (%d changed lines)" % (args.out_path, len(changes)))
    say("wrote %s" % json_path)
    return 0


if __name__ == "__main__":
    sys.exit(main())
