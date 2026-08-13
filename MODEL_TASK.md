\# AviUtl Free Render FPS — Phase 1



\## Goal



Investigate whether AviUtl2 can render a project at an output frame rate different from the project frame rate.



Example:



\- Project FPS: 30

\- Desired output FPS: 60

\- Project FPS must NOT be changed



This phase is research-first.



Do NOT immediately implement a full encoder plugin.



\## Available references



\- `reference/x264guiEx`

\- `reference/aviutl2\_sdk`



Use these as primary technical references.



\## Main questions



Determine:



1\. How AviUtl2 represents project FPS.

2\. How total frame count is calculated.

3\. How an output plugin receives frames.

4\. Whether an output plugin can request arbitrary frames.

5\. Whether it can request frames more frequently than the project FPS.

6\. Whether AviUtl2 exposes timestamp-based rendering.

7\. Whether rendering is fundamentally frame-index based.

8\. Whether output FPS is hard-linked to project FPS.

9\. Which function or layer controls the render loop.

10\. Whether this can be solved using only public plugin APIs.



\## x264guiEx analysis



Trace the x264guiEx output path.



Find:



\- output plugin entry point

\- project/output information structures

\- FPS retrieval

\- frame count retrieval

\- video frame request functions

\- current frame index handling

\- encoder input loop



Do not just describe files.



Trace the actual call chain.



\## Required documents



Create:



`docs/render\_pipeline.md`



Include a concrete diagram:



AviUtl2 project

→ timeline

→ render request

→ rendered frame

→ output plugin

→ x264guiEx

→ encoder



For every stage, list:



\- relevant functions

\- structs/classes

\- source files

\- public API vs internal behavior



Also create:



`docs/free\_fps\_feasibility.md`



Classify feasibility as:



A. Possible entirely with public plugin API



B. Possible with unusual/plugin API combinations



C. Requires hooking or internal patching



D. Requires modifying AviUtl2 itself



Every conclusion must cite exact source locations.



Do not guess.



\## Prototype



If the API allows it, create a minimal experimental plugin under:



`experiments/render\_probe/`



The plugin should only log:



\- project FPS

\- total project frames

\- requested frame index

\- implied timestamp

\- output callback order



Do not implement interpolation.



Do not fake higher FPS by duplicating frames.



The objective is to determine whether AviUtl2 can truly render at a different sampling rate.

