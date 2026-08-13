# AviUtl2 内部渲染候选点调查

## 阶段结论

本阶段找到了从公开输出回调进入 AviUtl2 私有视频工作器的稳定调用关系，也在运行时确认了整数输出帧进入对象求值回调的时序。最重要的新事实是：AviUtl2 在对象/滤镜求值边界确实构造了 `double` 秒值，但在 30 FPS 实测中它严格等于对象局部整数帧除以 30。

尚未定位到写入 `OBJECT_INFO.time` 的内部函数，也没有发现接收任意时间戳或分数帧的场景渲染入口。因此目前没有“可靠 Hook 点”，不满足制作内部 no-op Hook 原型的前提。按第二阶段约束，本阶段只保留无修改的公开滤镜观察探针，不写死地址、不安装 detour。

这仍属于可继续调查的 C 级路径：已经确认存在私有渲染边界，但未证明它能被安全地改造成子帧求值。当前证据也不足以升级为 D 级；尚不能断言必须修改 AviUtl2 本体。

## 证据范围与版本

- 分析对象：`aviutl2_v2.1.4/aviutl2.exe`
- SHA-256：`ED8AA51A80017839C232F35E7D3F6CB5E56FD09E8E13604726119CFB7C67CE89`
- x64 image base：`0x140000000`
- 无导出符号、无 CodeView/PDB；存在 LTCG 信息
- 文中的地址均为**模块相对 RVA**，只适用于上述二进制。运行时绝对地址必须加 ASLR 后的模块基址。
- 静态分析使用 PE section、`.pdata` 函数范围、字符串交叉引用和反汇编调用关系。未知符号均使用中性名称。

证据强度分为：

1. **运行时确认**：探针实际调用和 QPC/线程日志。
2. **SDK 契约**：官方头文件的公开结构和函数签名。
3. **静态强证据**：明确字符串 xref、参数寄存器、调用关系、格式常量。
4. **静态线索**：只有类型名/字符串，尚未绑定到可解释函数。

## 当前可证实的调用链

```text
输出插件调用 OUTPUT_INFO::func_get_video(integer frame, format)
  -> RenderCandidate_A / getVideo 宿主桥（RVA 0x22A6C0）
     - 整数请求帧 + 输出区间基址
  -> RenderCandidate_C / 私有视频渲染/缓存工作器（RVA 0x2206B0）
     - 仍以 32 位整数位置循环
  -> [尚未定位：场景帧 -> 对象局部 frame/time 的构造点]
  -> TimelineEvalObserver / FILTER_PROC_VIDEO
     - const OBJECT_INFO{frame:int, time:double}
     - 对象状态/滤镜效果求值
  -> [尚未定位：后续场景合成与最终格式转换]
  -> RenderCandidate_A 返回宿主复用画面缓冲区
  -> 输出插件 func_get_video 返回
```

运行时 QPC 证明 `FILTER_PROC_VIDEO` 位于 `func_get_video` 调用区间内。帧 320 的两次请求分别触发两次对象回调；详见 `docs/runtime_probe_results.md`。

SDK 输出入口仍是纯整数：`reference/aviutl2_sdk/output2.h:35-57` 的 `OUTPUT_INFO` 保存 `rate/scale/n`，并声明 `void* (*func_get_video)(int frame, DWORD format)`。没有时间戳参数。

## 八个目标阶段的证据状态

| 阶段 | 当前定位 | 状态 |
|---|---|---|
| 1. 输出帧索引进入 AviUtl2 | `RenderCandidate_A`, RVA `0x22A6C0` | 已有静态强证据与运行时外部调用证据 |
| 2. 帧索引转换为时间轴位置 | `TimelineEvalCandidate_B`，地址未知 | 只确认转换结果存在，写入点未找到 |
| 3. 对象状态计算 | `FILTER_PROC_VIDEO` 前后 | 运行时确认该边界收到对象局部 frame/time |
| 4. 动画插值计算 | 未定位 | 无可靠函数/参数证据 |
| 5. 特效计算 | 公开 `func_proc_video` 是效果链中的可观察点 | 已确认时序，但不是最早时间控制点 |
| 6. 场景合成 | 私有 `RenderVideoWorker` 字符串与工作器调用链 | 只有部分静态线索，未绑定完整签名 |
| 7. 最终画面缓冲区生成 | `RenderCandidate_C` 到 `RenderCandidate_A` 之间 | 返回缓冲区已确认；具体生成函数未拆分 |
| 8. 返回给输出插件 | `RenderCandidate_A` | 已确认返回复用缓冲区 |

## 候选总览

类型定义沿用本阶段要求：类型 1 只接收整数帧；类型 2 接收整数帧并在内部转为浮点时间；类型 3 直接接收时间戳/浮点/有理时间；类型 4 已处于画面生成后期。

| 候选 | 类型 | 模块 / RVA | 证据强度 | 子帧潜力 | 当前判断 |
|---|---:|---|---|---|---|
| OutputCallbackTableCandidate | 4 | `aviutl2.exe+0x22A4C0` | 静态强 | 无 | 只负责输出回调表/边界 |
| RenderCandidate_A | 1 | `aviutl2.exe+0x22A6C0` | 静态强 + 运行时外部 | 未见 | 最可靠的动态追踪起点，不是自由时间入口 |
| RenderCandidate_C | 1 | `aviutl2.exe+0x2206B0` | 静态强 | 未见 | 私有视频/缓存工作器，仍使用整数位置 |
| TimelineEvalCandidate_B | 2（待证） | 未定位 | 运行时结果线索 | 理论上最高 | 必须找到 `OBJECT_INFO.time` 写入点才能确认 |
| TimelineEvalObserver | 3（只读观察） | 公开 `FILTER_PROC_VIDEO` | SDK + 运行时 | 接口有 double，但不可写 | 证明浮点秒存在，不是可用 Hook 点 |
| RenderCandidate_D | 未分类 | `RenderVideoWorker` 字符串族 | 静态线索 | 未知 | 尚未把字符串绑定到时间参数函数 |
| AudioCandidate_A | 1 | `aviutl2.exe+0x22A8F0` | 静态强 | 未见 | 公开音频回调桥 |
| AudioCandidate_B | 1 | `aviutl2.exe+0x220D60` | 静态强 | 未见 | 整数乘除转换/音频工作器；字段语义未完全确认 |

## 候选详细记录

### OutputCallbackTableCandidate

- 所属模块：`aviutl2.exe`
- RVA / 范围：`0x22A4C0`，`.pdata [0x22A4C0, 0x22A6C0)`
- 特征：引用 `Plugin::ExternalOutputFile::save`；通过多个 `LEA` 安装 `0x22A6C0`、`0x22A8F0`、`0x22AA10`、`0x22AA20`、`0x22AA30` 等回调地址
- 调用者：输出文件/输出服务初始化路径
- 被调用函数：回调表安装及日志/辅助函数 `0x200400`
- 参数：尚未完整恢复
- 证据：字符串 xref + 连续函数指针构造
- 是否可能支持子帧：否；这是输出插件 ABI 边界，没有时间参数
- Hook 风险：中。改动会影响所有输出插件，但仍然太晚，单独 Hook 只能改变请求调度

### RenderCandidate_A

- 所属模块：`aviutl2.exe`
- RVA / 文件偏移 / 范围：`0x22A6C0` / `0x229AC0` / `[0x22A6C0, 0x22A8E7)`
- 中性用途：公开 `func_get_video` 的宿主桥
- 字符串证据：引用 `Plugin::ExternalOutputFile::getVideo`（字符串 RVA `0x3CCB20`）
- 字节前缀：`48 89 5C 24 10 48 89 74 24 18 48 89 7C 24 20 41 54`
- 参数证据：入口将 `EDX -> ESI`、`ECX -> R12D`，说明两个公开参数均为 32 位整数；它分派 `YUY2/PA64/HF64/YC48` 格式常量。两个参数的语义顺序由公开 ABI 与格式分派联合推断，不能仅凭寄存器移动命名。
- 关键行为：读取全局 `[0x1404E7918]`，计算 `r8d = [global+0x30] + requested_integer`，以 `RDX` 传输出描述、`R9D` 传内部格式并调用 `0x2206B0`
- 调用者：`0x22A4C0` 构造的回调表，最终由输出插件 `OUTPUT_INFO::func_get_video` 调用
- 被调用函数：`RenderCandidate_C` (`0x2206B0`) 及格式转换分支
- 是否可能支持子帧：当前没有。参数和基址加法都是整数
- Hook 风险：高。它是所有输出/预览共用边界之一，且 Hook 它只能重新排列整数请求，不能凭空生成 `1/60` 时间状态
- 推荐用途：动态断点起点，用来限定一次目标 `func_get_video` 请求的下游调用树

### RenderCandidate_C

- 所属模块：`aviutl2.exe`
- RVA / 文件偏移 / 范围：`0x2206B0` / `0x21FAB0` / 约 `[0x2206B0, 0x22080D)`
- 字节前缀：`40 55 53 56 57 41 56 41 57 48 8D 6C 24 D9`
- 调用者：`0x228F70`、`0x22A6C0`、`0x22B040`、`0x22B600`、`0x22C400`
- 参数证据：`RCX` 为上下文、`RDX` 为输出描述、`R8D` 为整数请求位置、`R9D` 为格式/内部代码
- 关键行为：读取 `[context+0x38]` 与 `[context+0x80]`，对 `R8D` 做整数比较/夹取，按 1 递增循环；调用 `0x2219D0`，最后调用 `0x221C00`
- 被调用函数：`0x2219D0` / `0x221C00` 显示 4 字节键哈希、bucket 遍历和引用计数特征，暂称缓存辅助函数，不能断言其完整语义
- 是否可能支持子帧：未见。当前反汇编路径没有浮点时间参数或整数到浮点的明显转换
- Hook 风险：很高。该工作器有多个预览/输出调用者，并涉及缓存、范围与引用计数；错误修改可能破坏并发和资源生命周期
- 分类说明：按签名为类型 1；如果最终画面已在其后很快生成，它在控制时机上也可能接近类型 4，但现有证据不足以精确划界

### TimelineEvalCandidate_B

- 所属模块 / 地址：未知；这是待定位的中性候选，不是假定存在的函数名
- 调用者：应位于 `RenderCandidate_C` 下游、`FILTER_PROC_VIDEO` 上游，但尚未建立直接静态调用边
- 被调用函数：可能包括对象状态、动画、效果求值；均未确认
- 参数：未知
- 运行时证据：输出帧 320 在对象回调中变为 `frame_s=240`、对象 `frame=80`、`time=80/30`；转换发生在请求 begin 与对象回调之间
- SDK 证据：`reference/aviutl2_sdk/filter2.h:328-348` 同时定义 `int frame`、`double time` 和场景绝对 `frame_s/frame_e`
- 是否可能支持子帧：理论上可能，是目前最早值得寻找的位置；但实测只见整数导出的时间
- Hook 风险：未知且预期很高。必须先确认时间存储寿命、线程归属、缓存键和嵌套调用恢复语义
- 下一步定位方法：在观察探针回调入口对 `OBJECT_INFO.time` 内存设置写入监视，回溯最后写入者；同时以 `RenderCandidate_A` 条件断点限定到输出请求，避免预览噪声

### TimelineEvalObserver

- 所属模块：公开 SDK 滤镜处理边界；实验实现位于 `experiments/timeline_eval_probe/timeline_eval_probe.cpp`
- 函数：`FILTER_PLUGIN_TABLE::func_proc_video(FILTER_PROC_VIDEO*)`
- 调用者：AviUtl2 私有对象/效果处理链
- 参数：`FILTER_PROC_VIDEO::scene` 与 `object`；两者均为 const 指针
- SDK 证据：
  - `reference/aviutl2_sdk/filter2.h:320-325`：场景有理帧率 `rate/scale`
  - `:328-348`：对象整数帧、double 秒、绝对起止帧
  - `:383-388`：视频滤镜回调参数
  - `:421-431`：以 `double offset` 查询对象输出参数/图像对象
- 运行时证据：两次 frame 320 请求都观察到 `frame=80`、`time=2.6666666666666665`
- 是否可能支持子帧：接口能表达 double 秒和 double offset，但回调收到的是只读状态；公开 API 没有“以任意绝对时间渲染整场景”入口
- Hook 风险：观察模式低；把这里当时间控制点则不可行，因为时间已构造完成且参数 const

### RenderCandidate_D

- 所属模块：`aviutl2.exe`
- 字符串：
  - `Render::RenderVideoWorker::renderingTargetObject`（RVA `0x3D0168`）
  - `Render::RenderVideoWorker::requestRenderingFrame::<lambda>`（RVA `0x3D01A0`）
  - `RenderSection`（RVA `0x3CFD20`）
- 调用者 / 被调用函数 / 参数：尚未恢复
- 证据：RTTI/诊断字符串只证明相关类/闭包存在，不能据此命名某段未知反汇编
- 是否可能支持子帧：未知
- Hook 风险：未知。没有函数边界和参数证据前禁止制作特征码 Hook

### AudioCandidate_A / AudioCandidate_B

- `AudioCandidate_A`：RVA `0x22A8F0`，引用 `Plugin::ExternalOutputFile::getAudio`；入口参数和全局偏移均按整数处理，然后调用 `0x220D60`
- `AudioCandidate_B`：RVA `0x220D60`，范围 `[0x220D60, 0x220F55)`；使用 `imul`、`cqo`、`idiv` 把一个整数坐标与上下文字段换算后调用音频工作器 `0x220BA0`
- 参数：确切字段含义尚未恢复，不能把 `+0x74/+0x78/+0x94` 武断命名为某个 rate/scale 字段
- 辅助证据：RVA `0x271F00` 初始化 `ProjectVideoRate` 默认 30、`ProjectVideoScale` 默认 1、`ProjectAudioRate` 默认 44100，但没有证明这些设置字段就是上述上下文偏移
- 是否可能支持子帧：当前路径是整数乘除并截断；未发现浮点时间输入
- Hook 风险：很高。视频时间改写若不同时处理音频坐标转换，会造成 A/V 漂移或区间错位

## x264guiEx 对宿主边界的交叉验证

x264guiEx 的 AviUtl2 构建在 `reference/x264guiEx/x264guiEx/auo.h:38-49` 包含 `output2.h`，并把源码中的 `func_get_video_ex` 映射为 AviUtl2 的 `func_get_video`。

实际输出链为：

```text
GetOutputPluginTable
  -> func_output2
  -> func_output
  -> init_enc_prm / check_output
  -> video_output
  -> video_output_inside
  -> enc_out
  -> oip->func_get_video_ex(i_frame, format)
  -> 像素转换
  -> 写线程把像素送入编码器 stdin
```

精确证据：

- `reference/x264guiEx/x264guiEx/x264guiEx.cpp:102-138`：输出表与 `GetOutputPluginTable`
- `reference/x264guiEx/x264guiEx/x264guiEx.cpp:263-367`：`func_output` 初始化、检查与任务分派
- `reference/x264guiEx/x264guiEx/x264guiEx.cpp:373-402`：AviUtl2 包装 `func_output2`
- `reference/x264guiEx/x264guiEx/encode/auo_video.cpp:455-501`：编码器 `--frames` 和 `--fps` 直接来自 `oip->n/rate/scale`
- `reference/x264guiEx/x264guiEx/encode/auo_video.cpp:791-1005`：`enc_out` 以 `i_frame` 顺序循环，并在 `:975-980` 请求整数帧
- `reference/x264guiEx/x264guiEx/encode/auo_video.cpp:704-733`：写线程只写编码器 stdin，不请求帧
- `reference/x264guiEx/x264guiEx/encode/auo_encode.cpp:857-867` 与 `:1715-1732`：时长为 `n * scale / rate`

这条真实插件链没有额外的时间戳字段；它与 AviUtl2 私有 `getVideo` 桥的整数参数证据一致。

## 重点问题状态矩阵

| 问题 | 当前结论 | 证据/限制 |
|---|---|---|
| 内部时间是否完全由整数帧表示 | 不能断言完全如此 | 输出/私有桥是整数；滤镜边界存在 double 秒 |
| 是否存在浮点时间 | 是 | `OBJECT_INFO.time/time_total`，运行时已记录 |
| 是否存在有理数时间基 | 是 | `SCENE_INFO.rate/scale`；输出也有 `rate/scale` |
| 动画插值依据帧号还是时间 | 未确认 | 未定位动画插值函数 |
| 是否存在子帧计算 | 未观察到 | 实测 `time=frame*scale/rate`；double 仅说明可表达，不说明实际采样 |
| 当前时间是否储存在全局变量 | 未确认 | 只发现输出上下文全局指针，不等同于场景时间全局 |
| 是否可以临时修改当前时间 | 未确认 | 公开回调的 `OBJECT_INFO` 为 const；私有写入点未找到 |
| 是否可单独调用场景渲染函数 | 未确认 | 没有恢复签名、上下文和资源生命周期 |
| 渲染函数是否可重入 | 未确认 | 运行时已显示跨线程，不能假定可重入 |
| 是否依赖主线程 | 未确认 | 输出线程与对象处理线程不同；具体调度者未知 |
| 缓存是否以整数帧为 key | 有强烈线索但未证明全部 | `0x2206B0` 整数循环；下游缓存辅助显示 4 字节 key |
| 子帧是否导致缓存错误 | 风险很高，尚未实测 | 若 key 只含整数帧，两个子帧会碰撞 |
| 音频是否使用同一时间系统 | 未确认 | 私有音频路径有独立整数换算和工作器 |

## 为什么没有制作内部 Hook 原型

用户要求第一版内部 Hook 只能在找到可靠点后制作，并且只能打印原值、不得改变结果。当前只有两个可靠地址：整数 `getVideo` 桥和整数视频/缓存工作器。Hook 它们能记录整数帧，却不能验证“内部渲染时间变量”。真正关键的 `OBJECT_INFO.time` 写入点仍未知。

在这种状态下对 `0x22A6C0` 或 `0x2206B0` 安装 detour，只会得到一个已被公开探针证明过的整数帧日志，并引入版本、栈展开、线程和缓存风险；它不满足原型目标。因此本阶段有意停止在公开 no-op 观察探针。

## 下一轮最小动态调查建议

1. 只对上述 SHA-256 的主程序启用调试符号记录和模块基址记录。
2. 在 `RenderCandidate_A` 以请求帧 320 为条件断点，缩小调用窗口。
3. 在 `TimelineEvalObserver` 回调入口读取 `OBJECT_INFO*`，对 `time` 字段地址设置硬件写入监视。
4. 回溯最后写入者，记录完整模块相对调用栈、线程、参数寄存器和 32–64 字节特征。
5. 先验证该写入者在顺序、非顺序和重复请求中是否稳定；不修改值。
6. 再检查其上游缓存 key 是否含 double/有理时间，以及视频和音频是否共享状态。

只有写入者和恢复语义均稳定后，才允许制作“打印原值、不修改”的内部 no-op Hook。
