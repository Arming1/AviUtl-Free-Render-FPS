# Upstream research, 2026-10-17

Scope: AviUtl2 version status, public API for time-based rendering, automated export, x264guiEx upstream, prior art.
Method: web fetches/searches on 2026-10-17. Page summaries were produced by a fetch tool (small model), so exact wording is paraphrased; items marked **unverified** could not be confirmed from a primary page. No binaries were downloaded.

## Summary

- **Hook will break**: official page lists AviUtl2 releases v2.1.5 ... v2.1.13 (latest 2.1.13, 2026/10/10) after the hooked v2.1.4. Pinned size/RVA/image-size check will reject every one of them; each needs re-validation (not verified here whether the internal code changed). Implication: hook will break; plan a per-version offset table or signature scan.
- **No public API for fractional/time-based frame retrieval**: `OUTPUT_INFO::func_get_video(frame, format)` and `EDIT_HANDLE::rendering_scene_video(int frame, ...)` still take integer frames; changelog (2026/10/10) shows no time-based video API. (`func_time_to_frame` exists only for input plugins, 2025/8/16.) Implication: public API not possible for sub-frame evaluation; hook remains required.
- **Scriptable export now exists via a general plugin (.aux2)**: `EDIT_HANDLE::open_project_file(file, show_confirm)`, `create_project(...)` and `output_file(file, output_plugin_name, param, func_project_config)` were added 2026/9/19. Implication: CLI-free test automation possible (a small aux2 plugin can open a project and start an export with a named output plugin). Not tested.
- **No documented AviUtl2 command-line export** found. A Yahoo!知恵袋 answer (2026/4/6) says batch output is unsupported (no API at that time); that API gap was later partially filled by `output_file` (2026/9/19). Implication: CLI test automation not documented; use the aux2 route.
- **AviUtl 1.10 CLI works**: documented form `aviutl.exe project.aup -op out.mp4 -q` (single blog source, flag meanings inferred) plus AviUtl Control (auc_*) tools. Implication: Win32 host test automation possible, partly unverified.
- **New SDK header cache2.h and project-stored output settings (FLAG_PROJECT_CONFIG, 2026/5/10)**: x264guiEx 4.11 uses the latter. Implication: fork should merge 4.11/4.12 if it wants the same behaviour on newer hosts; note output settings may be stored per project.
- **x264guiEx upstream is at 4.12 (Aug 4)** plus master commits to Sep 19; the fork base should be checked against 4.10-4.12 (audio fix for AviUtl2 >= 2.1.3, per-project settings, output2.h 20260718).
- **Prior art**: nothing found for real sub-frame output-FPS rendering. Implication: no reuse; unverified.

## 1. AviUtl2 version status

- Official page (KENくん, spring-fragrance.mints.ne.jp/aviutl/), Information section: 2026/09/27 v2.1.11; 2026/09/30 v2.1.11a; 2026/10/04 v2.1.12; 2026/10/10 v2.1.13 (latest; installer + zip + `aviutl2_sdk.zip` all dated 2026/10/10).
- Past versions page (`oldver2.php`) lists after 2.1.4: 2.1.5, 2.1.6, 2.1.6a, 2.1.7, 2.1.7a, 2.1.8, 2.1.9, 2.1.10, 2.1.11, 2.1.11a, 2.1.12, 2.1.13. No dates or change notes on that page. Release dates for 2.1.5-2.1.10 therefore **unverified**.
- The official pages show no changelog text. The best changelog found is the SDK mirror `CHANGELOG.md` (aviutl2/aviutl2_sdk_mirror, by sevenc-nanashi; auto-updated by GitHub Actions; entries by date, not version, SDK-side only). Whether it is a faithful copy of the official SDK notes is **unverified**. Relevant entries:

| Date | Item |
|---|---|
| 2025/8/16 | [aui2] multi-track (`func_set_track`) and time-based loading (`func_time_to_frame`) |
| 2025/10/5 | [aui2][auo2] InitializePlugin / UninitializePlugin |
| 2026/2/23 | [auo2] still-image output flag in OUTPUT_PLUGIN_TABLE |
| 2026/5/10 | [auo2] flags/functions to keep output settings in the project file (`FLAG_PROJECT_CONFIG`, load/save_project_config) |
| 2026/5/17 | InitializeCache; cache2.h added to the mirror |
| 2026/5/30, 8/16 | [cache2] more CACHE_HANDLE functions; `clear_image_cache()` / `clear_audio_cache()` |
| 2026/8/2 | [aux2] callback thread of `rendering_scene_video()/audio()` changed; `rendering_object_video()/audio()` added |
| 2026/9/19 | [aux2] EDIT_HANDLE gained `create_project`, `open_project_file`, `save_project_file`, `output_file`; EDIT_INFO `background`; CHANGE_EDIT_STATE event |
| 2026/10/10 | `get_output_image_param()/get_output_audio_param()` nullptr fix; layer color get/set; SCENE_INFO `origin_id` |

- Nothing in the changelog mentions: changes to `OUTPUT_INFO::func_get_video`, multithreaded rendering, timeline-evaluation internals, or sub-frame rendering. Absence in a summary changelog does not prove the internals are unchanged.
- Observed from x264guiEx: it updated `output2.h` to a 2026-07-18 version (commit 2026-07-23), and fixed audio output "AviUtl2 2.1.3 and later" (4.12), so 2.1.3 changed host audio behaviour (details **unverified**).

## 2. Public API for time-based / sub-frame rendering

- `output2.h` (mirror `include/aviutl2_sdk/output2.h`): OUTPUT_INFO has `rate`, `scale`, `n`, `func_get_video(int frame, DWORD format)` (formats BI_RGB, PA64, HF64, YUY2, YC48), `func_get_audio`, `func_is_abort`, `func_rest_time_disp`, `func_set_buffer_size`. No time-based accessor, no output-rate override, no motion-blur sub-frame request. `OUTPUT_PLUGIN_TABLE` flags: VIDEO, AUDIO, IMAGE, PROJECT_CONFIG.
- `plugin2.h` EDIT_HANDLE: `rendering_scene_video(int frame, ...)` and `rendering_object_video(object, int frame, apply_effect, ...)` are integer-frame, asynchronous, return RGBA in a callback, and fail while an output is running ("出力中等は失敗します"). `create_project(w,h,rate,scale,sample_rate,...)` sets the scene rate but is project-level.
- Only time-related API found: input plugins' `func_time_to_frame` (2025/8/16), which maps time to frame for the input side, not for output.
- Author statement about supporting output FPS different from project FPS: **none found**. The official page and changelog contain none.

## 3. Automated / command-line export

AviUtl2:
- No documented command-line arguments found on the official page, the SDK README, or searches. **Unverified / likely none.**
- Yahoo!知恵袋 Q&A (asked 2026/4/5, answered 2026/4/6): batch output "not supported; no API exists, plugins could not build it". Predates the 2026/9/19 API.
- Scriptable route (documented in `plugin2.h`): a general plugin (.aux2) calls `EDIT_HANDLE::open_project_file(LPCWSTR file, bool show_confirm)` then `EDIT_HANDLE::output_file(LPCWSTR file, LPCWSTR output_plugin, void* param, void(*func_project_config)(void*, PROJECT_FILE*))`. `output_file` only starts the export, cannot be used under reference/edit lock, and returns false if an output is in progress. Completion detection: possibly via `register_event_listener` CHANGE_EDIT_STATE (**unverified** that it signals export end). The `func_project_config` callback lets the caller push output settings when the output plugin has `FLAG_PROJECT_CONFIG`.
- `aviutl2-cli` (sevenc-nanashi) is a plugin/script development tool (symlink/deploy), not an export CLI.

AviUtl 1.10:
- Documented example (memo.eightban.com/bat/encode_aviutl, blog, 2025/6 per search summary): `"C:\app\aviutl\aviutl.exe" "X.aup" -op "X.mp4" -q`. Flags `-op` (output path) and `-q` are inferred from the example, not defined; AviUtl version unstated; output plugin selection syntax not documented there. **Unverified.**
- AviUtl Control (auc_exec, auc_openprj, auc_plugout <plugin number>, auc_wait, auc_close, auc_exit) drives the GUI by window messaging; see gist deflis/718426c0e2f53f1927f0 and the same blog. 
- AviUtl 1 built-in 「バッチ出力」 (File menu, via 「バッチ登録」 in output dialog) is GUI-only.

## 4. x264guiEx upstream

- Latest release: **4.12**, 04 Aug (year not shown on page; commit dates show 2026). Notes: fixes missing audio with AviUtl2 2.1.3+ (tag commit 93ae1ba).
- 4.11 (26 Jul): per-project output settings save/restore on AviUtl2.
- 4.10 (06 Jun): removed ini export via file filter; fixed ffmpeg Opus error and neroaacenc 2-pass; Chinese translation.
- Earlier AviUtl2: 4.08 (20 Dec, requires beta19+, logs to AviUtl2 log window, benchmark mode), 4.09 (28 Dec, uninstaller, cwd workaround).
- master after 4.12: 2026-08-04 docs; 2026-08-04 added AviUtl2 deploy/smoke-test scripts and exit-batch exit-code logging; 2026-09-12 zh.lng update; 2026-09-19 merge of PR #17 (nsYW). Also 2026-07-23 "update output2.h to 20260718 version".
- Merge candidates: the 4.11 project settings (uses `FLAG_PROJECT_CONFIG`; interacts with the fork's output-FPS setting storage), the 4.12 audio fix, the output2.h update, and the smoke-test scripts (may help host validation). Diffs were not inspected.

## 5. Prior art

- Searches (Japanese and English) found no plugin or script that renders at a higher output FPS through true sub-frame evaluation, and no write-up of ExEdit motion-blur internals. Results were generic (VFR->CFR duplication, frame interpolation tools). **Unverified; could not answer.**
- Related non-answers: AviUtl 1 VFR->CFR option duplicates/drops frames; interpolation plugins synthesise frames. Both are excluded by project rules.

## Sources

- Official: http://spring-fragrance.mints.ne.jp/aviutl/ and https://spring-fragrance.mints.ne.jp/aviutl/oldver2.php (KENくん; fetched 2026-10-17)
- SDK mirror (unofficial, sevenc-nanashi): https://github.com/aviutl2/aviutl2_sdk_mirror (CHANGELOG.md, include/aviutl2_sdk/plugin2.h, include/aviutl2_sdk/output2.h; commits Apr 26 - Oct 10, 2026)
- rigaya: https://github.com/rigaya/x264guiEx/releases, .../releases/tag/4.12, .../commits/master
- Yahoo!知恵袋 batch output Q&A: https://detail.chiebukuro.yahoo.co.jp/qa/question_detail/q13326967463 (2026/4/5-6)
- AviUtl 1 CLI/AUC example: https://memo.eightban.com/bat/encode_aviutl ; https://gist.github.com/deflis/718426c0e2f53f1927f0
- Karoterra AviUtl2 compatibility notes: https://scrapbox.io/karoterra/自作したもののAviUtl2対応状況
- aviutl2-cli: https://github.com/sevenc-nanashi/aviutl2-cli

## Unanswered / unverified

- Per-version changelog for 2.1.5 - 2.1.13 on the official side (no text on official pages); release dates of 2.1.5 - 2.1.10.
- Whether any 2.1.x changed the internal render path the hook targets (needs binary analysis, not done).
- Documented AviUtl2 CLI arguments (none found); AviUtl 1.10 option definitions.
- Whether CHANGE_EDIT_STATE (or another event) signals export completion for aux2-driven tests.
- Prior art for sub-frame output FPS.
