# Subframe Evaluation Validation（Phase 4）

## 结论

**成功，但结论严格限定于 AviUtl2 v2.1.4 的本次构建和测试场景。**

在公开输出插件仍只请求整数帧 `80`、工程仍为 30 FPS 的条件下，将对象求值前的内部 `double` 帧坐标从 `80.0` 临时改为 `80.5` 后：

- 内部整数坐标保持 `80`；
- 最终状态选择点仍保留 `double = 80.5`，没有丢弃小数；
- 下游滤镜回调收到 `OBJECT_INFO.frame = 80`、`OBJECT_INFO.time = 2.6833333333333331` 秒；
- 第 80 帧的最终 RGB 图像哈希从 `875555f88e3def2b` 变为 `e8cc2a2b6ddd098f`；
- 同一次对照中的帧 0、1、160 哈希完全不变；
- 输出回调完成，AviUtl2 没有崩溃并继续响应。

因此这个内部 `double` 坐标不是只供报告使用：它进入了实际对象动画/场景求值，并影响最终合成画面。Phase 4 的成功条件“非整数内部时间产生不同渲染场景”已经满足。

## 构建与边界

- AviUtl2：v2.1.4，`aviutl2.exe` SHA-256 `ED8AA51A80017839C232F35E7D3F6CB5E56FD09E8E13604726119CFB7C67CE89`。
- 映像大小：磁盘文件 5,228,544 字节；PE `SizeOfImage` 5,402,624 字节。
- 调试观察器最终构建 SHA-256：`5652DBC4C11E93A2076FAA37B955DFA37743CFD507A46E777E52C5C5D1DF8E77`。
- 所有地址均为模块 RVA；没有使用 ASLR 后的固定绝对地址。
- 没有补丁代码段，没有修改工程 FPS，没有修改编码器 FPS，也没有写 `OBJECT_INFO.time`。
- 观察器只在一个匹配调用的栈上执行一次 8 字节写入；函数返回后该值自然消失。

SDK 对 `SCENE_INFO.rate/scale` 和 `OBJECT_INFO.frame/time` 的定义见 `reference/aviutl2_sdk/filter2.h:321-345`。Phase 3 已用硬件写断点确认 `OBJECT_INFO.time` 的直接写入点为 RVA `0x20954a`，并把更早的 `TimelineStateBuildCandidate_C` 定位到 RVA `0x2662d0`；见 `docs/object_time_writer_analysis.md:53-62,175-181,211-223`。

## 测试场景

专用工程为 `experiments/subframe_eval_probe/subframe_test_30fps.aup2`：

- 1920×1080，30/1 FPS；
- 总输出帧数 161，索引 0–160；
- 一个 100 px 白色圆形对象，生命周期覆盖 0–160；
- X 坐标从 -500 线性动画到 +500；
- Phase 3 的 `timeline_eval_probe` 滤镜附着在对象上，只记录回调字段，不改图像。

选择帧 80 的原因是 `80.5` 位于对象有效区间内部，排除了先前“对象终点为 80”造成的端点裁剪歧义。

输出由 Phase 1 的 `Render Pipeline Probe` 驱动。它调用公开的 `OUTPUT_INFO::func_get_video(frame, BI_RGB)` 并对完整 1920×1080 RGB 缓冲区计算 FNV-1a 64 位哈希；调用和哈希代码见 `experiments/render_probe/render_probe.cpp:136-173`。

## 修改点

相关内部布局是以 `[rbp+0x170]` 为基址的临时求值状态：

| 位置 | 含义 | 本次值 |
|---|---|---|
| `[rbp+0x17c]` | 整数帧坐标，state +0x0c | `80`，未修改 |
| `[rbp+0x180]` | double 帧坐标，state +0x10 | `80.0 → 80.5` |
| `[rbp+0x188]` | double 残差，state +0x18 | `0.0`，未修改 |

执行位置：

1. RVA `0x2662d0`：候选状态构造函数入口；命中时 `R8D = 80`。
2. RVA `0x2663ad`：写整数 `80` 到 `[rbp+0x17c]`。
3. RVA `0x2663b8`：整数转换为 double。
4. RVA `0x2663bd`：写 `80.0` 到 `[rbp+0x180]`。
5. **RVA `0x2663c5`：硬件执行断点；在这里把刚生成的局部 double 从 `80.0` 改为 `80.5`。**
6. RVA `0x266642`：最终状态选择结束后的硬件执行断点，确认整数仍为 `80`、double 仍为 `80.5`、残差仍为 `0.0`。
7. 后续 RVA `0x20954a` 才把派生后的秒数写入 SDK 的 `OBJECT_INFO.time`；实验没有碰这个地址或目标字段。

观察器的三处 RVA 常量见 `experiments/subframe_eval_watch/subframe_eval_watch.cpp:17-19`，受守卫保护的写入见同文件 `:331-343`。

入口还记录到 `stack_arg8_flag = 0`、`stack_arg9_double = 0`。这表明本次公开输出路径走的是普通整数输入分支，而不是 Phase 3 发现的私有 double 输入分支。换言之，本实验验证的是“普通路径构造出的独立 double 状态是否被后续求值真正消费”，不是假装找到了公开的浮点参数。

## 实测数据

### 只记录基线

观察器日志：`experiments/subframe_eval_watch/phase4_baseline_stable_observer.log`

```text
candidate_entry_match ... thread_id=34404 incoming_r8d=80 stack_arg8_flag=0 stack_arg9_double=0
after_initial_store ... integer_position=80 double_position=80 residual_position=0
after_final_selection ... integer_position=80 double_position=80 residual_position=0
detached ... completed=1 mode=log
```

滤镜回调：

```text
frame=80 frame_total=161 time_seconds=2.6666666666666665 frame_s=0 frame_e=160
```

输出日志 `experiments/subframe_eval_probe/baseline_stable.log`：

```text
frame=80 sample_hash_fnv1a64=875555f88e3def2b
output_callback_end ... requests=5
```

### 80.5 修改实验

观察器日志：`experiments/subframe_eval_watch/phase4_modified_observer.log`

```text
candidate_entry_match qpc=230144749219 thread_id=34404 incoming_r8d=80
after_initial_store qpc=230144751340 integer_position=80 double_position=80 residual_position=0
double_coordinate_modified qpc=230144751630 before=80 requested_after=80.5 write_ok=1 readback=80.5
after_final_selection qpc=230144753110 integer_position=80 double_position=80.5 residual_position=0
detached ... completed=1 mode=modify
```

滤镜回调摘录保存在 `experiments/subframe_eval_watch/phase4_timeline_probe_excerpt.log`：

```text
baseline ... frame=80 time_seconds=2.6666666666666665
modified ... frame=80 time_seconds=2.6833333333333331
```

差值为：

```text
2.6833333333333331 - 2.6666666666666665
= 0.0166666666666666 s
= 0.5 * scene_scale / scene_rate
= 0.5 / 30 s
```

这同时验证了整数 `OBJECT_INFO.frame` 没变，而 SDK 报告的时间来自保留下来的小数坐标。

输出日志 `experiments/subframe_eval_probe/modified_frame80.log`：

| 请求帧 | 基线哈希 | 80.5 实验哈希 | 结果 |
|---:|---|---|---|
| 0 | `a2e8a890ea888feb` | `a2e8a890ea888feb` | 相同 |
| 160（首次） | `5fc6a0a5af83526b` | `5fc6a0a5af83526b` | 相同 |
| 80 | `875555f88e3def2b` | `e8cc2a2b6ddd098f` | **不同** |
| 1 | `dc261dc1170ea07f` | `dc261dc1170ea07f` | 相同 |
| 160（重复） | `5fc6a0a5af83526b` | `5fc6a0a5af83526b` | 相同 |

测试工程中唯一随时间变化的可见量是圆形对象的 X 位置，记录滤镜又明确不改图像。因此“第 80 帧完整 RGB 缓冲区改变、其他对照帧不变”支持如下因果结论：小数坐标改变了对象位置动画的求值，并传递到最终场景合成。当前探针只保留哈希，没有保留两张完整帧，所以这里不声称精确移动了多少像素。

## 调用时序与线程

修改实验的 QPC 频率为 10,000,000 Hz：

```text
output func_get_video(80) begin  qpc=230144747928  thread=35400
candidate 0x2662d0 entry       qpc=230144749219  thread=34404
double changed at 0x2663c5     qpc=230144751630  thread=34404
final state at 0x266642        qpc=230144753110  thread=34404
filter callback                qpc=230145351965  thread=27060
output func_get_video(80) end   qpc=230145400666  thread=35400
```

因此：

- 公开输出回调线程、内部状态构造/求值线程、滤镜回调线程是三个不同线程；
- 修改发生在 `func_get_video(80)` 进入之后、滤镜回调和最终缓冲区返回之前；
- 未来 Hook 不能假设状态构造和插件回调在同一线程，也不能使用无锁的单一全局“当前时间”。至少需要按渲染任务/线程关联并处理并发输出。

## 受影响系统

本次直接证实：

- 内部最终 double 帧坐标可保留小数；
- `OBJECT_INFO.time` 的下游生成使用该小数；
- 对象的位置动画插值使用或受该小数状态影响；
- 场景合成和最终 RGB 输出随之改变；
- 整数 `OBJECT_INFO.frame`、工程帧率、输出请求帧号都可以保持不变。

本次没有覆盖或不能推广到：

- 所有内置/第三方特效是否都使用同一 double 坐标；
- 场景嵌套、摄像机、脚本、粒子、视频解码等子系统；
- 音频时间系统和音画同步；
- 多个并行输出任务或预览与输出同时发生时的可重入性；
- 私有 double 输入分支的真正调用者和语义。

## 缓存行为

两次输出都实际命中了 RVA `0x2662d0` 的帧 80 调用，因此目标结果并非完全由一个绕过求值阶段的旧缓冲区直接返回。修改值在 RVA `0x266642` 仍为 `80.5`，最终哈希也改变，说明本次调用中小数没有被整数帧缓存键静默吞掉。

但这还不能证明所有 AviUtl2 缓存支持任意子帧：

- 帧 80 在每个输出会话中只请求一次；
- 两次测试是两个独立输出回调，会话级缓存可能已重置；
- 当前输出探针重复的是帧 160，不是被修改的帧 80；
- 未追踪纹理、对象、特效、媒体解码器等各级缓存键。

因此缓存结论仅为“本次完整求值调用中的 80.5 未被丢弃”。生产 Hook 仍必须解决以整数帧为键的缓存污染/错误命中问题。

## 稳定性与调试器清理

成功基线和修改实验都完成全部 5 次视频请求，输出回调正常返回，目标进程保持响应。没有修改模块代码或项目文件。

开发中发现观察器第一次稳定基线之后，跨线程 DR 清理句柄缺少 `THREAD_SUSPEND_RESUME` 权限，导致部分线程保留观察器的硬件断点值。它没有改变渲染状态，但属于必须修复的调试器卫生问题。最终版本做了三项修复：

1. 清理线程时申请挂起权限，并在读取/写入上下文期间挂起线程；
2. 只清除地址精确匹配本观察器三个 RVA 的 DR0/DR1，保留其他调试器槽位；
3. 增加 `cleanup` 模式，并在正常启动时先清除属于本观察器的陈旧槽位。

修复后的恢复操作记录在 `experiments/subframe_eval_watch/phase4_cleanup.log`。随后再次只清理验证，`experiments/subframe_eval_watch/phase4_cleanup_verify.log` 报告 `thread_count=0`，且 AviUtl2 仍保持响应。对应实现见 `experiments/subframe_eval_watch/subframe_eval_watch.cpp:194-247,471-477`。

## 对 Hook 可行性的意义

RVA `0x2663c5` 不是建议直接硬编码到产品中的地址；它只是本构建里最小因果实验的注入点。实验已经回答了 Phase 4 的核心问题：在对象求值前修改内部 double 坐标，可以让同一个整数输出帧号产生真实的子帧场景状态。

这使项目继续保持在 **C 级：内部 Hook 可行但高风险**，而不是因“值在求值前被丢弃”升级为必须修改 AviUtl2 本体的 D 级。

下一阶段若继续，应把工作集中在：

- 从稳定调用关系或字节特征定位状态构造点，禁止绝对地址；
- 找到 double 坐标的上游生产者，优先改变输入而不是写栈局部量；
- 建立输出请求与渲染工作线程之间的可靠任务关联；
- 逐层验证缓存键、嵌套场景、特效和音频；
- 设计异常安全、版本校验和完全可恢复的 Hook 生命周期。

本文件不把一次 debug 注入包装成 FreeRenderFPS 实现，也不声称已具备 30→60 输出调度能力。
