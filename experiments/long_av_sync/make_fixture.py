#!/usr/bin/env python3
"""Generate long_sync_60fps.aup2: 30 min, 60 fps, audio beeps plus white flashes.

Usage:
    python -I experiments/long_av_sync/make_fixture.py [--audio PATH] [--out PATH]

Timeline (frames at 60 fps, 108000 frames = 1800 s):
  layer 1 (layer=0): audio object for beeps_30min.wav, frames 0..107999.
  layer 2 (layer=1): one full-screen white 図形 (背景) per second, 2 frames
                     long, starting at frame s*60 for s = 0..1799.
  Each beep (t = s s) and each flash (frame s*60) therefore start together.

output.file points into experiments/long_av_sync/out/ and is checked for that.

Unverified against AviUtl2 v2.1.12: the audio object keys (effect name
音声再生, file key ファイル=) are not present in any repo or sample .aup2.
The 音量/左右 keys are copied from 映像再生 in
D:\\ARZv2.0.1\\ARZv2.0.1\\sources\\試験用動画.aup2. See README.zh-CN.md.
"""

from __future__ import annotations

import argparse
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
FPS = 60
DURATION_S = 1800
TOTAL_FRAMES = FPS * DURATION_S  # 108000
FLASH_FRAMES = 2
PROJECT_NAME = "LongAVSync 60fps 30min"


def audio_block(index: int, audio_path: str, project_path: str) -> list[str]:
    return [
        f"[{index}]",
        "layer=0",
        "focus=1",
        f"frame=0,{TOTAL_FRAMES - 1}",
        f"[{index}.0]",
        "effect.name=音声再生",
        f"ファイル={audio_path}",
        "音量=100.00",
        "左右=0.00",
    ]


def flash_block(index: int, start_frame: int) -> list[str]:
    return [
        f"[{index}]",
        "layer=1",
        "focus=0",
        f"frame={start_frame},{start_frame + FLASH_FRAMES - 1}",
        f"[{index}.0]",
        "effect.name=図形",
        "図形の種類=背景",
        "サイズ=100",
        "縦横比=0.00",
        "ライン幅=4000",
        "色=ffffff",
        "角を丸くする=0",
        f"[{index}.1]",
        "effect.name=標準描画",
        "X=0.00",
        "Y=0.00",
        "Z=0.00",
        "Group=1",
        "中心X=0.00",
        "中心Y=0.00",
        "中心Z=0.00",
        "Group3=1",
        "X軸回転=0.00",
        "Y軸回転=0.00",
        "Z軸回転=0.00",
        "Group2=1",
        "拡大率=100.000",
        "縦横比=0.000",
        "透明度=0.00",
        "合成モード=通常",
    ]


def build(audio_path: str, out_aup2: str, output_mp4: str) -> str:
    out_dir = os.path.join(HERE, "out")
    if os.path.commonpath([os.path.abspath(output_mp4), out_dir]) != out_dir:
        raise SystemExit(f"output.file must be under {out_dir}: {output_mp4}")
    lines = [
        "[project]",
        "version=2011200",
        f"file={os.path.abspath(out_aup2)}",
        "display.scene=0",
        "preview.scene=0",
        "display.scene.list.label=",
        "[scene.0]",
        "scene=0",
        f"name={PROJECT_NAME}",
        "label=",
        "video.width=640",
        "video.height=360",
        f"video.rate={FPS}",
        "video.scale=1",
        "audio.rate=48000",
        f"output.file={os.path.abspath(output_mp4)}",
        "cursor.frame=0",
        "cursor.layer=0",
        "preview.frame=0",
        "display.frame=0",
        "display.layer=0",
        "display.zoom=10000",
        "display.order=0",
        "display.camera=",
        "display.grid.x=16,-16",
        "display.grid.y=16,-16",
        "display.grid.width=200",
        "display.grid.height=200",
        "display.grid.step=200.000000",
        "display.grid.range=10000.000000",
        "display.tempo.bpm=120.000000",
        "display.tempo.beat=4",
        "display.tempo.start=0.000000",
        "display.tempo.offset=0.000000",
    ]
    lines += audio_block(0, audio_path, out_aup2)
    for second in range(DURATION_S):
        lines += flash_block(1 + second, second * FPS)
    return "\r\n".join(lines) + "\r\n"


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[1])
    parser.add_argument("--audio", default=os.path.join(HERE, "beeps_30min.wav"))
    parser.add_argument("--out", default=os.path.join(HERE, "long_sync_60fps.aup2"))
    parser.add_argument(
        "--output-mp4",
        default=os.path.join(HERE, "out", "long_sync_60fps.mp4"),
        help="render target written into output.file (must be under out/)",
    )
    args = parser.parse_args(argv)
    text = build(os.path.abspath(args.audio), args.out, args.output_mp4)
    with open(args.out, "w", encoding="utf-8", newline="") as handle:
        handle.write(text)
    objects = sum(1 for line in text.split("\r\n") if line.startswith("[") and line[1:-1].isdigit())
    print(f"wrote {args.out} ({objects} objects)")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
