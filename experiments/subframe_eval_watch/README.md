# Phase 4 subframe evaluation watch

This build-specific, debug-only observer supports the verified AviUtl2 v2.1.4
whose on-disk size is 5,228,544 bytes and mapped PE `SizeOfImage` is 5,402,624
bytes. It uses hardware execution breakpoints; it never
patches the executable code section and never writes `OBJECT_INFO.time`.

Usage:

    subframe_eval_watch.exe <pid> <observer-log> 80 log
    subframe_eval_watch.exe <pid> <observer-log> 80 80.5
    subframe_eval_watch.exe <pid> <observer-log> cleanup

The first mode only observes the call. The second performs one guarded 8-byte
write to the current call's local internal state at RVA `0x2663c5`, changing
`[rbp+0x180]` from exactly 80.0 to 80.5 while leaving `[rbp+0x17c]` equal to
integer 80. It rechecks both fields at RVA `0x266642`, clears the observer-owned
debug-register slots, and detaches. The stack-local value disappears when that render call
returns; no persistent code or project mutation is made.

The cleanup-only mode removes DR0/DR1 only when their addresses match this
observer's three build-specific RVAs. It preserves unrelated debug-register
slots. Normal startup also removes stale owned slots before arming, and normal
detach suspends each live thread while clearing the observer-owned slots.

The validated Phase 4 run used an interior frame of a 161-frame object. The
project and object intentionally continue through frame 160, so the 80.5 sample
is not an endpoint-clipping test.

Do not run this observer on an AviUtl2 instance with unsaved work.
