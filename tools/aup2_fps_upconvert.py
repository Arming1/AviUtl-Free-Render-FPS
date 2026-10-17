#!/usr/bin/env python3
"""Retime an AviUtl2 .aup2 project to a new scene frame rate.

Give either an integer factor K (target rate = scene rate * K) or an exact target rate with --target-fps
(for example 66 or 60000/1001). All rate and frame arithmetic is exact (fractions.Fraction).

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
})
# Trackbar payloads after '|' (e.g. "0|27"). Meaning is undocumented, so none are accepted.
TRACK_PIPE_PAYLOADS = frozenset()
# Modes exempt from the "value count equals frame count" check (study section 2).
TRACK_COUNT_EXEMPT_MODES = frozenset({"再生範囲"})
# --precise: a track is resampled exactly when it is this mode with this parameter, or when its values are constant.
PRECISE_LINEAR_MODE = "直線移動"
PRECISE_LINEAR_PARAM = "0"
# --precise: tracks in these modes are left unchanged and do not make their object ineligible.
PRECISE_EXEMPT_MODES = frozenset({"再生範囲"})
# --precise: an object with exactly two frame= points that are equal has no segment; it is skipped with this reason
# (the same text as experiments/free_fps_export/retime.cpp precise_problem()).
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
# Refused when a line has this key and this exact value.
REFUSED_KEY_VALUES = (("コマ落ち", "1"),)

UTF8_BOM = b"\xef\xbb\xbf"
SUFFIX_JSON = ".convert.json"

RE_INT = re.compile(r"^\d+$")
RE_NUMBER = re.compile(r"^-?\d+(\.\d+)?$")
RE_TARGET_FPS = re.compile(r"^(\d+)(?:/(\d+))?$")
RE_OBJECT_HDR = re.compile(r"^\[(\d+)\]$")
RE_EFFECT_HDR = re.compile(r"^\[(\d+)\.(\d+)\]$")
RE_SCENE_HDR = re.compile(r"^\[scene\.(\d+)\]$")


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


def precise_resample(lines, effect_lines, section_of, object_frame_lines, frame_values, new_frames, k, notes,
                     extra_decimals=0):
    """Resample trackbar values at the new keyframes from the exact original curve (--precise).

    An object is resampled only when every trackbar of it (except PRECISE_EXEMPT_MODES) passes
    precise_track_problem, and its frame= list is not two equal points (PRECISE_ZERO_LENGTH_REASON);
    otherwise it keeps its old values and is listed as skipped.
    A resampled track is written with its own decimal count d plus extra_decimals.
    Returns (bodies, report): bodies maps a trackbar line index to its rewritten text, report is the
    "precise" object of the JSON (it has "extra_decimals" only when extra_decimals > 0).
    Tracks above PRECISE_NOTE_RATIO add a note (not a refusal).
    """
    tracks = {}
    for idx in effect_lines:
        value = lines[idx].partition("=")[2]
        if not is_trackbar(value) or value.split(",")[-2] in PRECISE_EXEMPT_MODES:
            continue
        tracks.setdefault(section_of[idx][1], []).append(idx)

    bodies = {}
    skipped = []
    track_reports = []
    applied = 0
    for obj_no, frame_idx in object_frame_lines.items():
        if obj_no not in tracks:
            continue
        frames, new = frame_values[obj_no], new_frames[obj_no]
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
        for idx, key, parts, values in parsed:
            places = max(decimal_places(x) for x in parts[:-2]) + extra_decimals
            scale = 10 ** places
            exact = [curve_value(frames, values, Fraction(f) / k) for f in new]
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
    return bodies, report


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
            precise_extra_decimals=0):
    """Return (output_bytes, changes, notes, warnings, summary).

    Give exactly one of:
      factor: integer K >= 2; the target rate is the scene rate times K (video.scale is kept);
      target: (numerator, denominator) of the exact output rate, as returned by parse_target_fps.

    Raises Refusal on any unsupported content. Frame-dependent script is refused unless
    allow_frame_scripts is set; then each occurrence is returned in warnings and notes.
    precise resamples trackbar values at the new keyframes (see precise_resample); summary then has "precise".
    """
    if (factor is None) == (target is None):
        raise ValueError("convert() needs exactly one of factor or target")
    if factor is not None and factor < 2:
        raise Refusal(None, "factor must be an integer >= 2, got %d" % factor)

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

        for ref_key, ref_value in REFUSED_KEY_VALUES:
            if key == ref_key and value == ref_value:
                raise Refusal(idx + 1, "%s=%s is not supported" % (ref_key, ref_value))

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

    # Pass 2: scene checks. All scenes must share one rate/scale.
    rates = set()
    scene_rate_lines = {}  # scene_no -> (video.rate line index, video.scale line index or None)
    for scene_no, idxs in sorted(scene_lines.items()):
        values = {}
        for idx in idxs:
            key, _, value = lines[idx].partition("=")
            if key in ("video.rate", "video.scale"):
                if not RE_INT.match(value) or int(value) == 0:
                    raise Refusal(idx + 1, "%s must be a positive integer, got %r" % (key, value))
                values[key] = (int(value), idx)
            elif key in SCENE_KNOWN_KEYS:
                if key in SCENE_FRAME_KEYS and not RE_INT.match(value):
                    raise Refusal(idx + 1, "%s must be an integer, got %r" % (key, value))
            elif any(marker in key for marker in SCENE_UNKNOWN_REFUSE_MARKERS):
                raise Refusal(idx + 1, "unknown scene key %r may hold frame/mark/range data" % key)
        if "video.rate" not in values:
            raise Refusal(None, "[scene.%d] has no video.rate" % scene_no)
        scale_value, scale_idx = values.get("video.scale", (1, None))
        if scale_idx is None:
            notes.append("[scene.%d] has no video.scale; read as 1 and written after video.rate" % scene_no)
        scene_rate_lines[scene_no] = (values["video.rate"][1], scale_idx)
        rates.add((values["video.rate"][0], scale_value))
    if len(rates) != 1:
        raise Refusal(None, "scenes do not share one video.rate/video.scale: %s"
                      % sorted(rates))
    (src_rate, src_scale), = rates

    if factor is not None:
        target_num, target_den = src_rate * factor, src_scale
    else:
        target_num, target_den = target
    rate_src = Fraction(src_rate, src_scale)
    rate_dst = Fraction(target_num, target_den)
    k = rate_dst / rate_src

    # Pass 3: object frame lists, and trackbars that depend on them.
    frame_values = {}
    for obj_no, idx in object_frame_lines.items():
        frame_values[obj_no] = parse_frames(lines[idx].partition("=")[2], idx + 1)
    counter_lines = []
    for (obj_no, eff_no), props in sorted(effect_props.items()):
        if props.get("effect.name", (None, None))[0] != COUNTER_EFFECT_NAME:
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

    for idx in effect_lines:
        section = section_of[idx]
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
                    % (idx + 1, marker.group(0), where, speed_text(k)))
            warnings.append(text)
            notes.append(text)
        if not is_trackbar(value):
            continue
        parts = value.split(",")
        mode, param = parts[-2], parts[-1]
        if mode not in TRACK_MODES:
            raise Refusal(idx + 1, "trackbar mode %r is not on the whitelist" % mode)
        if "|" in param:
            payload = param.split("|", 1)[1]
            if payload not in TRACK_PIPE_PAYLOADS:
                raise Refusal(idx + 1, "trackbar '|' payload %r is not on the whitelist" % payload)
        obj_no = section[1]
        if obj_no in frame_values and mode not in TRACK_COUNT_EXEMPT_MODES:
            count = len(parts) - 2
            if count != len(frame_values[obj_no]):
                raise Refusal(idx + 1, "trackbar has %d values but object [%d] has %d frame points"
                              % (count, obj_no, len(frame_values[obj_no])))

    new_frames = {}
    shortened = set()
    for obj_no, idx in object_frame_lines.items():
        new_frames[obj_no], was_short = rescale_frames(frame_values[obj_no], k, idx + 1)
        if was_short:
            shortened.add(idx + 1)
            notes.append("object at line %d shortened to 1 frame" % (idx + 1))
    check_layers(object_frame_lines, object_layer, object_scene, frame_values, new_frames)
    precise_bodies, precise_report = {}, None
    if precise:
        precise_bodies, precise_report = precise_resample(
            lines, effect_lines, section_of, object_frame_lines, frame_values, new_frames, k, notes,
            extra_decimals=precise_extra_decimals)

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
                set_line(idx, "%s=%d" % (key, round_half_up(int(value) * k)))

    timing_objects = []
    for obj_no, idx in object_frame_lines.items():
        old, new = frame_values[obj_no], new_frames[obj_no]
        set_line(idx, "frame=" + ",".join(str(x) for x in new))
        timing_objects.append({
            "line": idx + 1,
            "start_error_s": str(abs(Fraction(new[0]) / rate_dst - Fraction(old[0]) / rate_src)),
            "end_error_s": str(abs(Fraction(new[-1] + 1) / rate_dst - Fraction(old[-1] + 1) / rate_src)),
            "shortened": idx + 1 in shortened,
        })
    for idx in sorted(precise_bodies):
        set_line(idx, precise_bodies[idx])
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
            precise_extra_decimals=args.precise_extra_decimals)
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
