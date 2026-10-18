# 新会话交接说明（2026-10-18）

> 给接手本项目的 AI 会话：先完整读完本文件，再动手。所有对用户的回复一律用简体中文。

## 1. 项目是做什么的

**FreeRenderFPS**：基于 x264guiEx 的 AviUtl2 输出插件（`x264guiEx-FreeRenderFPS.aux2`），让“工程帧率”和“输出帧率”彼此独立。
例如 30 fps 工程输出真正的 60 fps：每个输出帧都在**新的时间点重新计算画面**，而不是复制帧、也不是只改元数据。这是项目的根本原则（见 `AGENTS.md` 末尾），任何改动都不得违背。

两条实现路线：
1. **Free FPS Export（主力，AviUtl2 v2.1.12 可用）**：菜单“编辑 → Free FPS Export...”。插件把 `.aup2` 工程按目标帧率**重定时**（改写对象帧号、场景帧率、轨道值等），再用公开 SDK 打开重定时后的工程并导出。
2. **内置 Hook（旧路线，只支持 AviUtl2 v2.1.4 x64）**：输出设置里的 “Enable Free Render FPS”。用户的 AviUtl2 是 v2.1.12，此路线基本不用，不要投入精力。

另有 AviUtl 1.x 适配器（`.auo`，v1.0.1 包），已多时未动。

这是用户出于兴趣但非常重视的项目，面向其他创作者公开发布（README 必须保留 AI 辅助开发声明）。

## 2. 当前状态

- **最新正式版：v1.3.1**（GitHub Release，标签 `v1.3.1`，新增独立进程导出）。仓库：https://github.com/Arming1/AviUtl-Free-Render-FPS
- 分支：`master`（发布用）、`aviutl1-adapter`（日常开发）。
- **CI**：`.github/workflows/ci.yml`，每次推送自动跑：Python 测试、C++ 测试 + Python/C++ 逐字节交叉核对、x64/Win32 插件编译、宿主实测数据复算（host-results）。结果在 Actions 页面的 Summary。用户希望**频繁推送、在 Actions 实时看到进度**。
- **独立进程导出（v1.3.1 已发布）**：起因：第三方滤镜 `SceneMotionBlur_K`（来自 `MotionBlur_K.aux2`）会让 AviUtl2 在**切换工程**时崩溃，而 Free FPS Export 原流程必须切换工程。新模式：原窗口不切换工程，另启动一个 aviutl2.exe（环境变量 `FREE_FPS_EXPORT_JOB=<job.ini>`），子进程里的插件打开重定时工程、导出、写 `result.ini`、自我结束；工程含该滤镜时对话框自动勾选。测试版在 `test_kit\plugin`（SHA256 开头 FA4DEFCA）。
  - **2026-10-18 实测通过**：`神.aup2` → 132 fps，`D:\tutorial\132.mp4`，ffprobe 为 132/1、2880 帧；逐帧哈希只在开头第 0–167 帧（静止片头）有相同帧，其余相邻帧全部不同；原窗口未崩溃，子进程自行结束。
  - 不含该滤镜的 `10 3.aup2`（66 fps）→ 60 fps：复选框默认不勾，走旧流程（同窗口切换后重开原工程），`D:\tutorial\10_3_60.mp4` 为 60/1、3490 帧；相同帧只有 6.95–7.18 s 的 3 小段（每段 2–3 帧），对应工程第 459–479 帧每 6 帧一切的子场景快切（用户确认原工程该处本来就短暂静止）。

## 3. 关键代码位置

| 内容 | 位置 |
|---|---|
| 重定时器 Python 参考实现（规则的“真相”） | `tools/aup2_fps_upconvert.py`，规则说明 `tools/aup2_fps_upconvert.md` |
| 重定时器 C++（插件内实际使用） | `src/x264guiEx-FreeFPS/x264guiEx/freefps_export/retime.cpp/.h` |
| Free FPS Export 对话框与导出状态机 | `src/x264guiEx-FreeFPS/x264guiEx/freefps_export/freefps_export.cpp` |
| 输出插件主体、编码设置、工程保存的设置 | `src/x264guiEx-FreeFPS/x264guiEx/x264guiEx.cpp` |
| 界面文字（中日英） | `frm/auo_mes.h/.cpp` + `x264guiEx.zh/ja/en.lng`（枚举与名字表顺序必须一致；.lng 里不能用分号；en.lng 含非 UTF-8 字节，须字节安全编辑） |
| 规则总表（给用户看） | `docs/RETIME_RULES.md`；限制 `docs/KNOWN_LIMITATIONS.md` |
| Python 测试 | `tests/aup2_upconvert/test_upconvert.py`（`python -I` 运行），长时测试 `run_long.ps1` |
| C++ 测试与交叉核对 | `experiments/free_fps_export/build_and_run.ps1`（retime_test、host_stub_test、crosscheck.py） |
| 宿主实测数据复算 | `tests/host_results/host_results.py` + `test_kit/7_*/data`、`test_kit/8_*/data` |
| 宿主实测工程与清单 | `test_kit/`（索引见 `test_kit/使用说明.zh-CN.md`） |
| 打包 | `tools/package_release.ps1 -ReleaseLabel vX.Y.Z -RunSelfTest` → `dist/`（不入库） |

## 4. 必须遵守的工作方式（用户的长期要求）

1. **先分析、后动手**：用户提出问题时，先只读调查、用大白话给方案，用户确认后再改代码。
2. **子代理分工**：主会话只负责计划、审查、提交；动手工作交给子代理。默认 **Haiku 5.5、思考深度至少 medium**（用户认为 low 不可靠）；较难的代码用 **Sonnet 5.5 high**；“极高”指 `xhigh`，从不用 `max`；**从不用 Opus 子代理**。至少同时开 2 个子代理，改的文件互不重叠。子代理的提示与汇报都用中文。
3. **Python 与 C++ 必须逐字节一致**：任何换算规则改动，先改 Python（参考实现）并写测试，再按 Python 的取值对齐 C++，最后跑 `build_and_run.ps1` 交叉核对全部一致。
4. **宿主行为不靠猜**：AviUtl2 的行为（移动方式、时间控制、丢帧等）一律先做最小探针工程让用户实测（放 `test_kit/` 新编号目录），用分析脚本量化（`experiments/subscene_rate/analyze_steps.py`，阈值 0.5 px），确认后再放行。探针数据（CSV）入库并接入 host-results。
5. **不确定就拒绝**：转换器遇到未验证的写法要明确拒绝并写清原因，绝不静默输出可能错误的工程。
6. **额度意识**：用户额度有限，避免无谓的长消息与重复调查。
7. **对外操作**（推送、发布 Release、删除）前确认用户意图；发布说明与 README 必须带 AI 辅助开发声明。

## 5. 不要做 / 小心的地方

- **绝不提交** `experiments/free_fps_matrix/fixture_60fps.aup2` 的本地改动（切分支前 `git stash push -- 该文件`，切回后 `stash pop`）。曾误提交过一次。**不要用 `git commit -a`**，按路径 add。
- 不要改 `reference/` 下的文件；`dist/`、`build/`、`test_kit/**/out/`、插件二进制、视频不入库。
- 不要把 Basic_S 脚本相关问题列入处理范围（用户说它本身有问题）。
- `コマ落ち=0` 会让 AviUtl2 闪退，保持拒绝。
- 版本号同时在 `auo_version.h`、`tools/installer/Program.cs`、`app.manifest`、`tools/installer/README.md`、两个打包脚本里，改版本要一起改；Win32 的 AviUtl 1.x `.auo` 共用同一版本号。
- 发布顺序：改版本 → 文档定稿 → 编译测试 → 提交 → 合进 master → 推送 → 打包核对 → 打标签 → `gh release create`（gh 在 `C:\Program Files\GitHub CLI\gh.exe`，git 代理 `http://127.0.0.1:7892`，需用户开代理）。
- 文档要短：上次已把 README/限制精简一半，新增内容保持简洁，规则细节放 `docs/RETIME_RULES.md`。

## 6. 独立进程导出实测步骤（2026-10-18 已通过；回归时照做）

1. 关闭 AviUtl2，运行 `test_kit\plugin\安装.ps1`（会请求管理员权限），确认哈希开头 `FA4DEFCA`。
2. 打开 `神.aup2` → 编辑 → Free FPS Export...：“在独立进程中导出”应已自动勾选并显示原因。
3. 目标帧率填 132，确定：应弹出第二个 AviUtl2 窗口，自动导出后自行关闭；原窗口不切换工程、不崩溃，最后提示导出完成（含编码摘要）。
4. 换一个不含该滤镜的工程：复选框默认不勾，流程与旧版相同。
5. 失败时让用户截图；排查可在 `free_fps_export.ini` 设 `[debug] enabled=1`、`keep_temp=1`（示例 `test_kit\6_时间控制换算\free_fps_export.ini.example`），或设环境变量 `FREE_FPS_EXPORT_KEEP_TEMP=1`，日志搜 `[FreeFpsExport]`。
6. 已知待确认：子进程启动阶段是否会触发工程加载回调（否则 90 秒后报“没收到 load 回调”）；子进程窗口会抢焦点。

## 7. 待办池（按价值排序，用户决定时机）

1. GitHub Action 第二期：推版本标签自动打包 + 构建出处证明；升级 Node.js 20 的 Action 组件。
2. 目标帧率过低（如 66→10）时按真实取样时刻去掉过短对象（目前拒绝）。
3. `ランダム移動`、`反復移動`、`移動量指定` 的探针实测与放行。
4. AviUtl 1.x 适配器重新实测。

## 8. 用户环境（2026-10-18）

- AviUtl2 v2.1.12：`D:\aviutl2 v2.1.12\aviutl2.exe`；数据目录 `C:\ProgramData\aviutl2\`（插件在 `Plugin\`，本插件在 `Plugin\x264guiEx-FreeRenderFPS\`）。可多开。
- 第三方插件（`C:\ProgramData\aviutl2\Plugin\`）：
  - `MotionBlur_K.aux2`（提供 **SceneMotionBlur_K**，切换工程会崩溃）
  - `LocusMotion.aux2`（移动方式，依赖帧率，未支持）
  - `TextEditor.aux2`、`import_midi_tempos.aux2`、`AnalogFuu.auf2`
  - 输出：`x265guiEx.auo2`、`gif_output.auo2`；输入：`lwinput.aui2`（L-SMASH Works）
- 第三方脚本（`C:\ProgramData\aviutl2\Script\`）：Basic_S 系列（不处理）、`@イージング.tra`（イージング（通常）已放行，補間移動版拒绝）、LocusMotion.tra2、AutoBlendLight、AutoBlur_K、CrushPNG、GPUピクセルソート、Glitch_T、LyricMotioN、NCSVisualizer、NagoEffect2、Pixelizer、ReflectionOnPane、SAM3、SomethingLikeGlitch、TA-* 系列、extra_blend_mode、ntsc-rs、パララックス_Rh、放射クリッピング、音MADミラー（.anm 与已改名的“（オブジェクト）.obj”）等。部分文件名是解压时编码错误的乱码，不影响使用。
- 用户常用工程：桌面 `10 3.aup2`（已成功导出 60 fps）、`神.aup2`（含 SceneMotionBlur_K）、ARZ 模板 `D:\ARZv2.0.1\`（66 fps、BPM 165，大量 時間制御；输出帧率宜取 66 的整数倍，如 132）。
