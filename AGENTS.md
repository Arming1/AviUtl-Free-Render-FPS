\# AGENTS.md



\## Project purpose



This project investigates and eventually implements free output render FPS for AviUtl2.



The main requirement:



Project FPS and output render FPS must be independently configurable.



Example:



30 FPS project

→ true 60 FPS render output



Changing metadata only is NOT acceptable.



Duplicating frames only is NOT acceptable.



\## Development rules



1\. Research the AviUtl2 SDK before writing implementation code.

2\. Use `reference/x264guiEx` as a real-world output plugin reference.

3\. Cite exact source files and symbols in technical conclusions.

4\. Do not assume API capabilities.

5\. Prefer minimal experimental probes before large implementations.

6\. Keep reverse-engineering/internal-hook work separate from public API work.

7\. Do not modify files under `reference/`.

8\. Put research documents under `docs/`.

9\. Put experimental code under `experiments/`.

10\. Put production code under `src/`.



\## Current phase



Maintenance of the released plugin (v1.3.1, 2026-10-18).

New sessions: read `docs/HANDOFF.zh-CN.md` first. It describes the project, the
current work, the working rules (analysis before edits, subagent policy,
Python/C++ byte-identical retimer, host probes before allowing new behaviour)
and what must not be touched.



绝对不要把“输出更多帧”误认为“在更多时间点重新计算场景”。

如果 60 个输出帧只是重复了 30 个场景状态，
就不算实现自由渲染帧率。