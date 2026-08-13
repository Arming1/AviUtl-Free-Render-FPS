# OBJECT_INFO.time hardware-write observer

This Phase 3 experiment attaches to an already running AviUtl2 process as a
Windows debugger. It reads the exact `time_addr` written by the timeline probe
and installs an 8-byte x64 hardware write breakpoint on every target thread.
It can also pre-arm the previous internal `double` source address to catch the
producer-side state copy before filter dispatch.

It does not write to OBJECT_INFO.time or patch AviUtl2 code. On a write hit it
records the thread, register state, instruction pointer, bytes around the
instruction pointer, loaded-module-relative address, and a best-effort x64
stack walk.

Usage:

    object_time_watch.exe <aviutl2-pid> <timeline-probe-log> <observer-log> [max-hits] [initial-source-hex]

The observer detaches without terminating AviUtl2 after max-hits hardware
breakpoint events (default: 16), or when Ctrl+C is used.

`initial-source-hex` is deliberately optional because internal state addresses
are allocation-specific and may change on the next render. The observer also
uses an execution breakpoint at AviUtl2 v2.1.4 RVA `0x209990` to discover the
current dispatch state. This is a build-specific research tool, not production
hook code.
