#!/usr/bin/env python3
"""Convert an AviUtl2 .aup2 project to K-times the scene frame rate.

Writes a new file; never modifies the input. Rules and refusals are described in
tools/aup2_fps_upconvert.md. Exit codes: 0 converted, 2 refused or usage error.
"""

import argparse
import hashlib
import json
import os
import re
import sys

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

# [scene.N] keys whose value is a frame number; multiplied by K.
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


def new_frames(frames, factor, line_no):
    """Apply the frame rewrite rule. Returns the new list of ints."""
    if len(frames) == 2:
        start, end = frames
        if end < start:
            raise Refusal(line_no, "frame= end %d is before start %d" % (end, start))
        return [start * factor, (end + 1) * factor - 1]
    if any(b <= a for a, b in zip(frames, frames[1:])):
        raise Refusal(line_no, "frame= intermediate points are not strictly increasing: %s"
                      % ",".join(str(x) for x in frames))
    start, middle, end = frames[0], frames[1:-1], frames[-1]
    result = [start * factor] + [m * factor for m in middle] + [(end + 1) * factor - 1]
    if any(b <= a for a, b in zip(result, result[1:])):
        raise Refusal(line_no, "frame= list is not strictly increasing after conversion")
    return result


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


def convert(data, in_path, out_path, factor, allow_frame_scripts=False):
    """Return (output_bytes, changes, notes, warnings).

    Raises Refusal on any unsupported content. Frame-dependent script is refused unless
    allow_frame_scripts is set; then each occurrence is returned in warnings and notes.
    """
    if factor < 2:
        raise Refusal(None, "factor must be an integer >= 2, got %d" % factor)

    has_bom = data.startswith(UTF8_BOM)
    body_bytes = data[len(UTF8_BOM):] if has_bom else data
    try:
        text = body_bytes.decode("utf-8")
    except UnicodeDecodeError as exc:
        raise Refusal(None, "input is not valid UTF-8: %s" % exc)

    records = split_records(text)
    lines = [body for body, _ in records]

    # Pass 1: map each line to its section.
    section = None
    section_of = []
    project_file_lines = []
    scene_lines = {}
    object_frame_lines = {}
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
            elif m_obj:
                section = ("object", int(m_obj.group(1)))
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

    # Pass 2: scene checks. All scenes must share one rate/scale.
    rates = set()
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
        if "video.rate" not in values or "video.scale" not in values:
            raise Refusal(None, "[scene.%d] has no video.rate/video.scale" % scene_no)
        rates.add((values["video.rate"][0], values["video.scale"][0]))
    if len(rates) != 1:
        raise Refusal(None, "scenes do not share one video.rate/video.scale: %s"
                      % sorted(rates))

    # Pass 3: object frame lists, and trackbars that depend on them.
    frame_values = {}
    for obj_no, idx in object_frame_lines.items():
        frame_values[obj_no] = parse_frames(lines[idx].partition("=")[2], idx + 1)
    notes = []
    warnings = []
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
                    "will run %d times faster and per-frame random sequences change"
                    % (idx + 1, marker.group(0), where, factor))
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

    # Pass 4: build the rewritten lines.
    project_idx = project_file_lines[0]
    new_bodies = list(lines)
    changes = []

    def set_line(idx, new_body):
        if new_body != lines[idx]:
            changes.append({"line": idx + 1, "old": lines[idx], "new": new_body})
            new_bodies[idx] = new_body

    out_abs = os.path.abspath(out_path)
    set_line(project_idx, "file=" + out_abs)

    for scene_no, idxs in scene_lines.items():
        for idx in idxs:
            key, _, value = lines[idx].partition("=")
            if key == "video.rate":
                set_line(idx, "%s=%d" % (key, int(value) * factor))
            elif key in SCENE_FRAME_KEYS:
                set_line(idx, "%s=%d" % (key, int(value) * factor))

    for obj_no, idx in object_frame_lines.items():
        frames = frame_values[obj_no]
        converted = new_frames(frames, factor, idx + 1)
        set_line(idx, "frame=" + ",".join(str(x) for x in converted))

    out_bytes = bytearray()
    if has_bom:
        out_bytes += UTF8_BOM
    for (body, eol), new_body in zip(records, new_bodies):
        out_bytes += (new_body + eol).encode("utf-8")
    return bytes(out_bytes), changes, notes, warnings


def same_path(a, b):
    if os.path.normcase(os.path.abspath(a)) == os.path.normcase(os.path.abspath(b)):
        return True
    return os.path.exists(a) and os.path.exists(b) and os.path.samefile(a, b)


def build_parser():
    parser = argparse.ArgumentParser(
        description="Convert an AviUtl2 .aup2 project to K-times the scene frame rate.")
    parser.add_argument("--in", dest="in_path", required=True, help="input .aup2 (never modified)")
    parser.add_argument("--out", dest="out_path", required=True, help="new .aup2 to write")
    parser.add_argument("--factor", dest="factor", type=int, required=True,
                        help="integer multiplier K >= 2")
    parser.add_argument("--force", action="store_true", help="overwrite existing outputs")
    parser.add_argument("--allow-frame-scripts", action="store_true",
                        help="convert despite frame-dependent script (obj.frame, rand(...)); prints a WARNING per occurrence")
    return parser


def main(argv=None):
    parser = build_parser()
    args = parser.parse_args(argv)

    def refuse(message):
        say(message, err=True)
        return 2

    if args.factor < 2:
        return refuse("REFUSED: --factor must be an integer >= 2, got %d" % args.factor)
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
        out_data, changes, notes, warnings = convert(
            in_data, args.in_path, args.out_path, args.factor,
            allow_frame_scripts=args.allow_frame_scripts)
    except Refusal as exc:
        return refuse(exc.message())

    for text in warnings:
        say(text, err=True)
    with open(args.out_path, "wb") as fh:
        fh.write(out_data)

    report = {
        "tool": "aup2_fps_upconvert",
        "factor": args.factor,
        "input": {"path": os.path.abspath(args.in_path), "sha256": sha256_hex(in_data)},
        "output": {"path": os.path.abspath(args.out_path), "sha256": sha256_hex(out_data)},
        "changed_line_count": len(changes),
        "changes": changes,
        "notes": notes,
    }
    with open(json_path, "w", encoding="utf-8", newline="\n") as fh:
        json.dump(report, fh, ensure_ascii=False, indent=2)
        fh.write("\n")

    say("wrote %s (%d changed lines)" % (args.out_path, len(changes)))
    say("wrote %s" % json_path)
    return 0


if __name__ == "__main__":
    sys.exit(main())
