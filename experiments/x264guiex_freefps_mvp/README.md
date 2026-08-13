# Phase 7 runtime result

Date: 2026-08-11

Project: `experiments/subframe_scheduler_test/subframe_scheduler_30fps.aup2`

Configuration:

- project: 30/1 fps, 161 frames;
- Free Render FPS: enabled;
- target: 60/1 fps.

Result file:

`experiments/subframe_scheduler_test/phase7_freefps_60_true.mp4`

`ffprobe -count_frames` result:

```text
video: H.264, 1920x1080, 60/1 fps, time_base 1/60,
       322 frames, 5.366667 s
audio: AAC, 44100 Hz, 5.366667 s
container: 5.366667 s, 49,690 bytes
```

Decoded `framemd5` result:

```text
decoded frames:       322
unique frame hashes:  322
adjacent equal:       0
even/odd pairs equal: 0
```

First eight decoded frame hashes:

```text
0  5568c041f5827500692cd1c517139c2b
1  1c24e76864d11008f6a67121475212ec
2  0f7f210e218cd27cbd08800a10c29b91
3  7712d41ef94771546622ee1ed49077e0
4  5d67cd5dc81b521619994fee6f214bc3
5  c807d58a4e645f167bbac74056af20ea
6  005a0b86efa3ea308ea6d867224c0639
7  bf36ac44b8f6a8bed142b3d6782c899b
```

Because public frame pairs `(0,0), (1,1), ...` were scheduled at coordinates
`(0.0,0.5), (1.0,1.5), ...`, zero equal even/odd pairs proves that the encoded
60 fps stream contains two independently evaluated temporal states per source
frame rather than duplicated 30 fps frames.

Control file `phase7_freefps_60.mp4` was exported with FreeFPS disabled and is
30/1 fps, 161 frames, and 5.366667 s.

