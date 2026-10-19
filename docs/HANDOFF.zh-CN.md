# 新会话交接说明（2026-10-19）

> 给接手本项目的 AI 会话：先完整读完本文件，再动手。所有对用户的回复一律用简体中文（包括很短的状态说明）。

## 1. 项目是做什么的

**FreeRenderFPS（用户简称 AFRF）**：基于 x264guiEx 的 AviUtl2 输出插件（`x264guiEx-FreeRenderFPS.aux2`），让“工程帧率”和“输出帧率”彼此独立。
例如 30 fps 工程输出真正的 60 fps：每个输出帧都在**新的时间点重新计算画面**，而不是复制帧、也不是只改元数据。这是项目的根本原则（见 `AGENTS.md` 末尾），任何改动都不得违背。

两条实现路线：
1. **Free FPS Export（主力，AviUtl2 v2.1.12）**：菜单“编辑 → Free FPS Export...”。插件把 `.aup2` 工程按目标帧率**重定时**（改写对象帧号、场景帧率、轨道值等），再用公开 SDK 导出。
2. **内置 Hook（旧路线，只支持 AviUtl2 v2.1.4 x64）**：基本不用，不要投入精力。

另有 **AviUtl 1.x 适配器**（Win32 `.auo`，Hook 拓展编辑 0.92，在小数帧时刻重新计算画面，`encode/freefps_aviutl1_hook.cpp`，发布包 v1.0.1）。**用户说很多创作者因 AviUtl2 的 AVX2 等配置要求仍在用 1.10**，所以它重要；但自 2026-08-20 起只测过一个 5 秒的简单工程，需要回归（见第 7 节）。

这是用户出于兴趣但非常重视的项目，面向其他创作者公开发布（README 必须保留 AI 辅助开发声明）。README 顶部已写明“开发状态：预览版（Beta）”。

## 2. 当前状态

- **最新发布版：v1.3.1**（GitHub Release，标签 `v1.3.1`）。仓库：https://github.com/Arming1/AviUtl-Free-Render-FPS
- 分支：`master`（发布用；2026-10-19 只单独摘入了 README 开发状态说明 `3a7cdf2`）、`aviutl1-adapter`（日常开发，含大量**未发布**改动，见下）。
- **CI**：`.github/workflows/ci.yml`，每次推送跑 Python 测试、C++ 测试 + Python/C++ 逐字节交叉核对、x64/Win32 编译、宿主实测数据复算（host-results）。用户希望频繁推送。
- **aviutl1-adapter 上未发布的改动（2026-10-18～19）**，每项都有宿主实测：
  - 三种按帧移动方式 `反復移動`、`ランダム移動`、`移動量指定` 的精确换算（test_kit/9，三轮）；`移動量指定` 按原文小数位数写；`ランダム移動` 只放行起点 0。
  - **次合成对齐**：只被导出场景里一个 シーン 对象（速度 100、不循环、起点为整帧）播放的次合成，在播放对象的时间线上取整（test_kit/10 实测通过）；否则写 WARNING（对话框与完成提示都会显示）。
  - **场景编号修复**：场景以 `[scene.N]` 段内的 `scene=` 值为准，不是段号 N（发布版起就有的隐患）。
  - **不保存也能导出**：独立进程模式用 `save_project_file` 写快照（test_kit/11、12 实测），默认勾选；未保存工程强制独立进程。
  - **按帧/随机滤镜**：`振動`、`ノイズ` 拒绝；`フレア` 仅 `点滅=0` 放行；`手ぶれ`、`ラスター` 放行（test_kit/13 自动导出 + 10 3.aup2 排除实验）。
- 测试版插件在 `test_kit\plugin\`（每次更新时旧版改名为 `.old-<哈希>` 保留）。

## 3. 关键代码位置

| 内容 | 位置 |
|---|---|
| 重定时器 Python 参考实现（规则的“真相”） | `tools/aup2_fps_upconvert.py`，规则说明 `tools/aup2_fps_upconvert.md` |
| 重定时器 C++（插件内实际使用） | `src/x264guiEx-FreeFPS/x264guiEx/freefps_export/retime.cpp/.h` |
| Free FPS Export 对话框与导出状态机 | `src/x264guiEx-FreeFPS/x264guiEx/freefps_export/freefps_export.cpp`（**没有自动化测试**；`experiments/free_fps_export/free_fps_export.cpp` 是早已分叉的实验版，host_stub_test 测的是它） |
| 界面文字（中日英） | `frm/auo_mes.h/.cpp` + `x264guiEx.zh/ja/en.lng`（枚举与名字表顺序一致；.lng 不能用分号；en.lng 含非 UTF-8 字节须字节安全编辑；.lng 编进插件资源） |
| 规则总表 / 限制 | `docs/RETIME_RULES.md` / `docs/KNOWN_LIMITATIONS.md` |
| Python 测试 | `tests/aup2_upconvert/test_upconvert.py`、`test_long_duration.py`（`python -I`） |
| C++ 测试与交叉核对 | `experiments/free_fps_export/build_and_run.ps1`（retime_test、host_stub_test、crosscheck.py；几分钟） |
| 宿主实测数据复算 | `tests/host_results/host_results.py` |
| 宿主实测工程与清单 | `test_kit/`（索引 `test_kit/使用说明.zh-CN.md`） |
| **无人值守导出（自动化测试）** | `experiments/host_runner/`：`host_export.py`、`batch_export.py`、`analyze_probe13.py`，用法见其 README |
| 打包 | `tools/package_release.ps1 -ReleaseLabel vX.Y.Z -RunSelfTest` → `dist/`（不入库） |

## 4. 必须遵守的工作方式（用户的长期要求）

1. **先分析、后动手**：用户提出问题时，先只读调查、用大白话给方案，用户确认后再改代码。
2. **子代理分工**：主会话只负责计划、写精确规格、审查、复跑测试、提交；动手工作交给子代理。用户明确要求用 **Haiku 5.5 子代理**，思考深度 medium 或 high（不用 low），从不用 Opus 子代理；改的文件互不重叠；提示与汇报用中文。**汇报时如实告诉用户分工与思考深度。** 子代理的报告要核对（数字抽查、diff 审查），它们常有小错（文件写错、证据过度推断）。
3. **Python 与 C++ 必须逐字节一致**：先写共用的精确规格（拒绝文字、常量名、检查顺序），Python 与 C++ 两个子代理并行实现，最后跑 `build_and_run.ps1` 交叉核对 0 不一致才提交。C++ 的 `%r` 等价写法是 `"'" + 值 + "'"`。
4. **宿主行为不靠猜**：先做最小探针（`test_kit/` 新编号目录），量化分析后再放行。**优先用便携 AviUtl2 无人值守导出**（第 6 节），只有界面类操作（对话框、在界面里新建写法）才请用户手动。探针结构必须照搬用户真实工程的用法（例如 振動 是作用于图层号更小的层的滤镜对象，手ぶれ 挂在 カメラ制御 上，フレア 是独立生成对象）。
5. **不确定就拒绝**：遇到未验证的写法明确拒绝并写清原因。但“同时出现”不等于“原因”：放行或拒绝前要有排除实验或探针证据（曾误把 手ぶれ/ラスター/フレア 列入拒绝，后经排除实验改正）。
6. **额度意识**：避免无谓的长消息与重复调查。
7. **对外操作**（推送 master、发布 Release、删除、下载文件）前确认用户意图；推送开发分支 `aviutl1-adapter` 属日常（用户希望频繁推送）。

## 5. 不要做 / 小心的地方

- **绝不提交** `experiments/free_fps_matrix/fixture_60fps.aup2` 的本地改动（切分支前 `git stash push -- 该文件`，切回后 `stash pop`）。**不要用 `git commit -a`**，按路径 add。
- 不要改 `reference/`；`dist/`、`build/`、`test_kit/**/out/`、插件二进制（含 `test_kit/11_快照保存/SnapshotSaveProbe.aux2`）、视频不入库。
- 不要动用户的 `C:\ProgramData\aviutl2\`、`D:\aviutl2 v2.1.12\`、桌面工程（只读复制）。
- 不要把 Basic_S 脚本相关问题列入处理范围。`コマ落ち=0` 会让 AviUtl2 闪退，保持拒绝。
- 版本号同时在 `auo_version.h`、`tools/installer/Program.cs`、`app.manifest`、`tools/installer/README.md`、两个打包脚本里。用户认为现在还不算正式版：以后版本建议带 `-beta.N` 后缀、GitHub Release 勾 Pre-release（v1.3.1 是否补勾待用户决定）。
- 发布顺序：改版本 → 文档定稿 → 编译测试 → 提交 → 合进 master → 推送 → 打包核对 → 打标签 → `gh release create`（gh 在 `C:\Program Files\GitHub CLI\gh.exe`，git 代理 `http://127.0.0.1:7892`）。
- 文档要短，规则细节放 `docs/RETIME_RULES.md`。

## 6. 无人值守导出（2026-10-19 起）

- 便携 AviUtl2：`build\host\aviutl2\`（v2.1.12 的运行文件 + `data\` 文件夹 → 设置与插件都在 `data\`，不碰 ProgramData；依据 `aviutl2.txt` 第 62–63 行）。`data\Plugin\` 有测试版插件、用户的第三方插件与 `data\Script\`（只读复制而来）。整个 `build\` 被 git 忽略，换机器需按 `experiments/host_runner/README.md` 重建。
- 原理：插件的独立进程子模式。`job.ini`（UTF-16 BOM，`[job] retimed/output/plugin=x264guiEx FreeRenderFPS/timeout_ms`）+ 环境变量 `FREE_FPS_EXPORT_JOB` 启动 `aviutl2.exe`，子进程导出后写 `result.ini` 并退出。
- 速度：探针约 3 秒/个；58 秒的 10 3.aup2 约 4 分钟/个。运行时会弹出 AviUtl2 窗口并抢焦点，开跑前告诉用户。
- 换算后导出：用 Python 转换器生成 `retimed.aup2` 再交给子进程（与插件逐字节一致）。仅为调查需绕过某条拒绝时，在临时脚本进程内改常量，**不改仓库代码**。

## 7. 待办池（用户决定时机）

1. **已对齐场景里的残留差异**：10 3.aup2 66→60 在已对齐的场景 1（1.2–1.7 秒，约 47% 像素）、2（4.0–5.8 秒）、8（12.2 秒）仍有差异，不是 フレア/手ぶれ，也不是一帧偏移，原因未知（可能是视频素材在次合成里的播放时间、运动模糊、精确模式等）。数据与脚本在上次会话的 scratchpad，结论在 `test_kit/13_按帧滤镜/结果.zh-CN.md`。
2. **“无法对齐”是否改为可选拒绝**：10 3.aup2 的场景 11、12 实测有 20 多秒的一帧偏移（转换后像原片早一帧）。
3. **AviUtl2 v2.1.13 / 2.1.14 已发布**（10/10、10/17，SDK 也更新）：建一份 2.1.14 便携副本（需用户下载），用 `batch_export.py` 把 test_kit 重跑并比对。
4. **AviUtl 1.x 回归**：用 `.exo`（文本）生成探针；需先问用户 AviUtl 1.10 的安装路径（C:\、D:\ 根目录下没找到）。待补的帧率组：51.2→60、51.2→59.94、66→60，外加次合成、随机滤镜。
5. `ノイズ` 探针：需用户在界面里加一次拿到写法；之后可自动测。`振動` 维持拒绝。
6. 用户正在向其他创作者征集真实工程（X / Discord）；收到后放本地（不入库），先只读分析再跑端到端。
7. 其它：GitHub Action 第二期；66→10 这类过低帧率的过短对象；`LocusMotion` 移动方式；独立进程窗口最小化/不抢焦点。

## 8. 用户环境（2026-10-19）

- AviUtl2 v2.1.12：`D:\aviutl2 v2.1.12\aviutl2.exe`；数据目录 `C:\ProgramData\aviutl2\`（本插件在 `Plugin\x264guiEx-FreeRenderFPS\`）。可多开。官网已有 2.1.14。
- 第三方插件：`MotionBlur_K.aux2`（SceneMotionBlur_K，切换工程会崩溃）、`LocusMotion.aux2`、`TextEditor.aux2`、`import_midi_tempos.aux2`、`AnalogFuu.auf2`、`x265guiEx.auo2`、`gif_output.auo2`、`lwinput.aui2`（L-SMASH Works）。第三方脚本见 `C:\ProgramData\aviutl2\Script\`（Basic_S 系列不处理）。
- 用户常用工程：桌面 `10 3.aup2`（66 fps，13 个场景，段号与场景 ID 不同；现可转换，场景 3–7、9–12 有对齐警告）、`神.aup2`（含 SceneMotionBlur_K 与 振動 → 现在会被拒绝，属预期）、ARZ 模板 `D:\ARZv2.0.1\`（66 fps）。
