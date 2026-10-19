# host_runner：便携 AviUtl2 无人值守导出（探针）

目的：用一份便携 AviUtl2 副本，走插件的子进程导出路径（`freefps_export.cpp` 的 `detect_child_job`），导出一个 `.aup2`，验证结果与手动导出一致。只在本机、Windows 下运行。

## 1. 建便携副本

以下内容都在仓库的 `build/host/`（已被 `.gitignore` 的 `**/build/` 忽略）。

- 从 `D:\aviutl2 v2.1.12\` 只复制运行所需文件到 `build\host\aviutl2\`：`aviutl2.exe`、`lua.dll`、`luaJIT.dll`、`aviutl2.txt`、`credits.txt`、`lua.txt`、`Default.aul2`、`default.word`、`effect.conf`、`style.conf`、`system.conf`、`preset.params`、`script.anm2/cam2/obj2/scn2/tra2`。
- 建空文件夹 `build\host\aviutl2\data\` 和 `data\Plugin\`。`data` 存在时 AviUtl2 为便携模式（`aviutl2.txt` 第 58–63 行），设置与插件文件都写到 `data` 下。
- 把 `C:\ProgramData\aviutl2\Plugin\x264guiEx-FreeRenderFPS\` 整个文件夹复制到 `data\Plugin\x264guiEx-FreeRenderFPS\`，再用 `test_kit\plugin\x264guiEx-FreeRenderFPS.aux2` 覆盖其中的 `.aux2`，并删除复制来的 `.aux2.before-precise`。测试版 `.aux2` 的 SHA256：`4A51112A3FF810171B01CA86A29AFEBA587EFF4AADDFCD0EC147F69D27F18835`。

脚本会拒绝没有 `data` 文件夹的 `aviutl2.exe`，防止误用安装目录（否则它会写到 ProgramData）。

## 2. host_export.py

```
python -I experiments/host_runner/host_export.py --host build/host/aviutl2/aviutl2.exe ^
    --project PROJECT.aup2 --output OUT.mp4 [--timeout 600]
```

- 在 `build\host\runs\<时间戳>\` 写 `job.ini`，然后带环境变量 `FREE_FPS_EXPORT_JOB=<job.ini>` 启动 `aviutl2.exe`，等待其结束；超时则杀掉并失败。
- `job.ini` 是 UTF-16LE 带 BOM、CRLF 的文件（与插件的 `create_unicode_ini` 一致）：

  ```
  [job]
  retimed=<.aup2 的绝对路径>
  output=<.mp4 的绝对路径>
  plugin=x264guiEx FreeRenderFPS
  timeout_ms=<timeout*1000>
  ```

  `plugin` 是插件注册名（`x264guiEx.cpp` 的 `output_plugin_table.name`）。项目直接作为 `retimed` 传入，只读，不保存、不修改。
- 读取同目录的 `result.ini`（`[result]` 的 ok / output / bytes / encoder / error），检查输出文件是否存在。退出码：0 成功；1 导出失败（超时、无 result、ok≠1、输出缺失）；2 参数或环境错误。
- 输出文件已存在时拒绝运行（先删除）。

## 3. 已验证的事实

运行：`test_kit\9_其他移动方式\B4_反復_30_p7.aup2` 导出到 `build\host\out\B4_auto.mp4`。

- 首次即成功，退出码 0，耗时 12.1 s。`result.ini`：`ok=1`，`bytes=58185`，`encoder=CRF 23 / medium / AAC (ffmpeg) 256kbps / mp4`，`error` 为空。没有重试。窗口和对话框未截图确认，成功依据是退出码、`result.ini` 与输出文件。
- 便携模式：`build\host\aviutl2\data\` 下生成了 `module.ini`、`Default\default.palette` 和各子文件夹（Alias、Backup、Log 等）；没有生成 `data\aviutl2.ini`。`module.ini` 的内容为 `[x264guiEx-FreeRenderFPS\x264guiEx-FreeRenderFPS.aux2] trust=1`，说明插件是从 `data\Plugin` 加载的。
- 用户环境未被改动（运行前后对比）：
  - `C:\ProgramData\aviutl2\aviutl2.ini`：2026-10-19 13:48:46.5441175Z（UTC），运行前后不变，大小 51767 字节。
  - `C:\ProgramData\aviutl2\Plugin\x264guiEx-FreeRenderFPS\x264guiEx-FreeRenderFPS.aux2`：2026-10-19 13:02:24.1070963Z（UTC），运行前后不变，大小 1673728 字节。
  - ProgramData 下运行后没有任何新增或修改的文件。
  - 源项目 `B4_反復_30_p7.aup2` 的 SHA256 运行前后相同（`0DD40E3F8D433770DED2EDD27D5C575D5890B582453DEBA2DC32284F4DA6DB3C`）。
- 与手动导出比对（`test_kit\9_其他移动方式\data\B4.csv` 与 `out\B4.mp4`）：
  - `analyze_steps.py` 的 `centroid_x` 逐帧比较：两边都是 150 帧，最大差 0.1916 px（第 134 帧），平均差 0.0898 px，超过 0.5 px 的帧数 0。
  - ffprobe：两者都是 1920x1080、yuv420p、30/1 fps、150 帧、音频 AAC 44.1 kHz 2 ch、时长 5.000 s。但编码不同：自动导出为 H.264 High（85611 bps 视频），手动为 HEVC Main（158428 bps）。
  - `ffmpeg -f framemd5` 逐帧比较：150 帧中相同的为 0（编码不同，预期如此）。

## 4. 限制

- 运行时会弹出 AviUtl2 窗口并抢焦点，只能在用户本机、用户在场时运行。
- 只能在 Windows 上运行，需要 `ffmpeg`/`ffprobe` 在 PATH 中（比对步骤用）。
- 便携副本是快照：插件一改动，需要重新执行第 1 节的复制步骤。
- 脚本不检查插件 `.aux2` 的版本，只检查 `data` 文件夹是否存在。
