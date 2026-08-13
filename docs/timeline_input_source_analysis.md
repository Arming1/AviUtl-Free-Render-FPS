# Timeline input source analysis（Phase 5）

## 结论

Phase 5 找到了两类上游来源，并完成了最小调度验证：

1. **普通输出路径仍从整数帧开始。** 实测 `func_get_video(i)` 进入 AviUtl2 后，`TimelineStateBuildCandidate` 的直接调用点为 RVA `0x2657e3`（返回 RVA `0x2657e8`）；入口 `R8D=i`，私有布尔参数为 `0`，私有 `double` 参数为 `0.0`。
2. **AviUtl2 内部确实已有传递完整 double 坐标的专用路径。** RVA `0x266000` 接收 `XMM2` 的 double，执行 `vcvttsd2si r8d,xmm6` 生成兼容整数，同时把未截断的 `xmm6` 作为私有 double 传给 RVA `0x2662d0`。这不是公开输出路径，而是至少被运动模糊等内部效果使用的路径。
3. 在普通输出调用关联期间，把 RVA `0x2662d0` 的**输入参数**从 `(flag=0,double=0)` 临时改为 `(flag=1,double=sample_index*0.5)`，可以让 30 FPS 工程得到 60 个按 1/60 秒递增的不同场景状态。实验没有再写 RVA `0x2663c5` 的局部状态，也没有写 `OBJECT_INFO.time`。

因此，double 坐标的上游机制真实存在；普通输出调度器没有公开或默认提供这个 double。当前 PoC 的 Hook 仍是 v2.1.4 专用实验，不是可发布的版本无关 Hook。

## 构建边界

- `aviutl2.exe`：v2.1.4，SHA-256 `ED8AA51A80017839C232F35E7D3F6CB5E56FD09E8E13604726119CFB7C67CE89`。
- 所有地址均为模块 RVA，不是 ASLR 后的绝对地址。
- 最终 watcher SHA-256：`DA924C4C9CE41F8951FE640ADA003426A21B18DBD6293AEDE54C165605585CD3`。
- SDK 公开边界仍只有 `OUTPUT_INFO.rate/scale/n` 和 `func_get_video(int frame, DWORD format)`；见 `reference/aviutl2_sdk/output2.h:35-57`。
- x264guiEx 的 AviUtl2 路径也只把整数 `i_frame` 传给该回调；见 `reference/x264guiEx/x264guiEx/auo.h:38-45`、`reference/x264guiEx/x264guiEx/encode/auo_video.cpp:907-980`。

## 普通输出调用链（动态确认）

```text
subframe_scheduler_probe.auo2
  -> OUTPUT_INFO::func_get_video(integer frame, BI_RGB)
  -> aviutl2.exe+0x22a6c0     输出视频回调桥
  -> 私有视频渲染/缓存路径
  -> aviutl2.exe+0x265590     普通状态包装器
  -> call at +0x2657e3
  -> aviutl2.exe+0x2662d0     TimelineStateBuildCandidate
  -> 对象/滤镜/合成求值
  -> 返回最终 RGB 缓冲区
```

最终日志 `experiments/subframe_scheduler_test/phase5_schedule_watch_final.log:1-7` 同时证明：

- 输出桥、状态构造器和普通调用点的字节签名全部匹配；
- 请求 0 的输出线程为 `29008`；
- 状态构造线程为 `4536`；
- 直接返回 RVA 是 `0x2657e8`；
- `integer_input=0`、`private_flag=0`、`private_double=0`；
- 输入参数改为 `flag=1,double=0` 后成功回读。

观察模式最初把邻近的另一标准包装器返回 RVA `0x265b64` 作为候选。实际 63 次输出中，该候选从未匹配；动态记录稳定命中 `0x2657e8`，所以最终构建只接受 `0x2657e8`。最终签名常量和字节模式见 `experiments/subframe_scheduler_test/subframe_scheduler_watch.cpp:15-35`。

## RVA 0x2662d0 的实测输入

Windows x64 调用约定下，本候选的相关输入为：

| 位置 | 普通输出实测值 | Phase 5 修改 | 说明 |
|---|---:|---:|---|
| `R8D` | 输出请求的整数帧 | 不修改 | 兼容整数坐标；`OBJECT_INFO.frame` 最终仍为截断整数 |
| `[RSP+0x40]` | `0` | `1` | 选择私有 double 输入分支的布尔参数 |
| `[RSP+0x48]` | `0.0` | `i*0.5` | 完整 double 帧坐标 |
| `RCX` | 稳定私有指针 | 不修改 | 本次运行中为 `0x1a06abb8290`；结构语义未命名 |
| `RDX` | 稳定私有指针 | 不修改 | 本次运行中为 `0x1a06a952740`；结构语义未命名 |

读取、守卫、写入和回读代码见 `subframe_scheduler_watch.cpp:337-407`。它只在以下条件全满足时修改：输出事务有效、没有重叠、直接调用者匹配、整数输入等于当前请求帧、参数地址可读写。任何失败都会停用后续修改。

## 已存在的内部 double 生产者

### DoubleProducer_A：RVA 0x266000

静态反汇编显示：

- 入口把 `XMM2` 保存到 `XMM6`；
- `0x2660bd`：`vcvttsd2si r8d,xmm6`，从 double 产生兼容整数；
- 随后把 `xmm6` 写入被调函数的私有 double 参数，并把私有布尔参数设为 1；
- `0x2660e7` 调用 RVA `0x2662d0`。

上游 RVA `0x1447d0` 会对 double 坐标做减法、步进和边界夹取，再调用 `0x266000`。RTTI 将该虚函数所属类族指向 `Effect::MotionBlurFilter`。这证明整数并非所有内部路径的唯一时间源；至少专用效果路径先拥有 double，再派生整数。

### DoubleProducer_B：RVA 0x265dd0

该包装器从调用者栈读取已有 double，保持整数参数独立，并以私有布尔参数 1 调用 RVA `0x2662d0`。调用者 RVA `0x13a4c0` 从一个私有对象的 `+0x10` 字段读取 double。RTTI 仅能把这个共享虚函数归入 `Effect::SceneChange` / `Effect::SceneChangeScript` 类族；函数槽精确语义尚未命名。

### 没有发现的内容

- 这几条链中没有发现单独的有理数时间结构或时间戳对象。
- 没有证据表明普通输出路径存在未使用的公开 timestamp 参数。
- 目前证据支持的内部表示是“double 帧坐标 + 独立/兼容整数坐标”，而不是已经暴露给输出插件的时间基。

## 30→60 mapper

映射器完全独立于编码器和 `OBJECT_INFO.time`：

```text
target_coordinate = output_sample_index * project_fps / output_fps
                  = output_sample_index * 30 / 60
                  = output_sample_index * 0.5
```

本 PoC 直接使用常量 `0.5`，不修改工程 `rate=30,scale=1`，也不包含编码器；实现见 `subframe_scheduler_watch.cpp:278-289`。输出探针仍用公开整数参数请求 0..59，见 `subframe_scheduler_probe.cpp:122-160`。整数参数在这里仅作为 60 个唯一任务/缓存键和事务序号，真正求值位置由内部 double 输入控制。

## 动态结果

完整 60 行结果在 `experiments/subframe_scheduler_test/phase5_sample_results_final.tsv`。前三个样本：

| 输出样本/请求帧 | 内部 double | `OBJECT_INFO.frame` | `OBJECT_INFO.time` | RGB FNV-1a | 圆心 X |
|---:|---:|---:|---:|---|---:|
| 0 | 0.0 | 0 | 0 | `a2e8a890ea888feb` | 459.5082 |
| 1 | 0.5 | 0 | 0.016666666666666666 | `50281afb6812c46f` | 462.9130 |
| 2 | 1.0 | 1 | 0.033333333333333333 | `dc261dc1170ea07f` | 465.9258 |

样本 1 的图像哈希不同于样本 0 和样本 2，圆心也位于两者之间。`OBJECT_INFO.time` 以 1/60 秒递增，而 `OBJECT_INFO.frame` 仍按 double 截断为整数。原始输出日志见 `phase5_schedule_output_final.log:1-7`，滤镜记录见 `phase5_schedule_object_info_final.log:2-4`。

60 个样本全部返回非空图像；60 个唯一帧请求全部命中状态构造器、通过调用者/帧号守卫并成功写入。附加的第一次帧 80 缓存诊断也成功写入，所以最终统计为 61 次命中/61 次应用。随后两次帧 80 请求被整数缓存短路，见缓存分析文档。

## Hook 适用性

### 实验适用

RVA `0x2662d0` 的入口参数是目前最早且已实测可控的位置。它比 Phase 4 的 `0x2663c5` 更上游，能让状态构造器自己完成整数/double 选择、裁剪和后续传播。

### 生产仍不适用固定地址

版本无关实现至少应同时匹配：

1. 输出桥的结构/格式分支特征；
2. 普通调用点 `call` 的相对目标和调用前“私有 flag=0、double=0”的设置模式；
3. 状态构造器入口与其“整数写入、整数转 double、私有 double 选择”控制流；
4. 内部专用路径的特征序列：`vcvttsd2si`、保存完整 double、设置 flag=1、调用同一构造器；
5. 每个构建的字节签名、映像大小/哈希和运行时自检。

仅匹配单一短字节串仍可能误命中；需要结构化反汇编验证和版本描述文件。当前 watcher 的签名检查及线程寄存器保存/恢复见 `subframe_scheduler_watch.cpp:15-35,150-275,470-606`。

## 尚未解决

- 普通输出如何安全地产生超过工程 `n` 的完整输出样本序列；本次只验证 60 个样本，不生成编码文件。
- 如何让重复整数请求绕过或区分 AviUtl2 的帧缓存。
- `RCX/RDX` 私有上下文的确切类型、生命周期和可否作为稳定任务键。
- 多个并发/嵌套输出请求的任务关联。
- 音频时间系统和 60 FPS 输出时的音视频同步。

