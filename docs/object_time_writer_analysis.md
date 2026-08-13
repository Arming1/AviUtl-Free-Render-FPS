# OBJECT_INFO.time writer analysis (Phase 3)

## 结论

在 AviUtl2 v2.1.4 中，`OBJECT_INFO.time` 的直接写入点已经通过 **8 字节硬件写断点**动态命中。它不是在插件中计算，也不是简单读取刚写好的 `OBJECT_INFO.frame` 再做除法。

直接写入函数（以下使用中性名称 `ObjectInfoPopulateCandidate_A`）同时构造 `SCENE_INFO` 和 `OBJECT_INFO`。它从内部求值状态读取：

- 一个整数帧坐标：内部状态 `+0x0c`；
- 一个独立的 `double` 帧坐标：内部状态 `+0x10`；
- 场景的整数 `rate/scale`。

正常的公开输出/预览路径中，内部 `double` 坐标最初由整数帧号转换而来，因此实测仍然是整数帧时间。但是更早的内部状态构造函数存在一个私有分支，可以接收并保留 `double` 坐标，同时把它截断为整数副本。这证明 AviUtl2 内部并非只能表达整数时间；真正值得继续验证的 Hook 点在该内部状态构造阶段，而不是已经太晚的 `OBJECT_INFO.time` 写入点。

## 调查对象与证据文件

- 主程序：`aviutl2_v2.1.4/aviutl2.exe`
  - SHA-256: `ED8AA51A80017839C232F35E7D3F6CB5E56FD09E8E13604726119CFB7C67CE89`
  - PE 无导出符号和可用 PDB，因此本文不给未知函数编造名称。
- SDK 定义：`reference/aviutl2_sdk/filter2.h:321-352`
  - `SCENE_INFO.rate/scale`：`filter2.h:323`
  - `OBJECT_INFO.frame`：`filter2.h:331`
  - `OBJECT_INFO.time`：`filter2.h:333`
- 最小无图像修改探针：`experiments/timeline_eval_probe/timeline_eval_probe.cpp:103-147`
  - 只记录回调参数地址、帧、时间、线程和 QPC；返回 `true`；不修改图像或时间。
- 硬件断点观察器：`experiments/object_time_watch/object_time_watch.cpp`
  - 不补丁 AviUtl2，不写 `OBJECT_INFO.time`。
- 有效动态日志：
  - `experiments/object_time_watch/phase3_runtime_valid.log`
  - `experiments/object_time_watch/phase3_entry_source_trace_final.log`
  - `experiments/object_time_watch/phase3_source_writer_trace.log`

地址均以 **模块 RVA** 为准。文中的 `0x00007ff66b......` 绝对地址只属于本次 ASLR 会话，本次 `aviutl2.exe` 基址为 `0x00007ff66b080000`，不能写死。

## 动态方法

1. 探针在真实 `func_proc_video` 回调中记录 `video->object` 和 `&video->object->time`。
2. 外部观察器对该 8 字节地址设置 x64 DR0 写断点，并对所有目标线程设置相同断点。
3. 命中后记录 RIP、完整寄存器、模块相对地址、附近机器码和调用栈。
4. 在滤镜调度入口 RVA `0x209990` 设置执行断点，用当次 `R8` 找到内部状态的 `double +0x10`。
5. 对上一帧仍存活的 `double` 地址预先设置 DR1 写断点，成功命中生产者线程上的上游状态复制。
6. 最后结合命中地址周边和调用者的静态反汇编，定位最初构造整数/`double` 时间坐标的函数。

无效的早期日志 `experiments/object_time_watch/phase3_runtime.log` 不作为证据：早期解析器曾把无 `0x` 前缀的地址错误处理。修正后日志明确显示 `watched=0x7ff66b5446b0`。

## 直接写入点：ObjectInfoPopulateCandidate_A

### 地址与指令

- 函数范围：RVA `0x209480` 至约 `0x2095cb`
- `OBJECT_INFO.frame` 写入：RVA `0x209506`
  - `mov dword ptr [r11+08h], ecx`
- `OBJECT_INFO.time` 写入：RVA `0x20954a`
  - `vmovsd qword ptr [r11+10h], xmm2`
- 硬件写断点异常报告的下一条 RIP：RVA `0x209550`
- 本次绝对写指令地址：`0x00007ff66b28954a`
- 本次异常 RIP：`0x00007ff66b289550`

写入目标是 AviUtl2 模块内的共享暂存区：

- `OBJECT_INFO` 基址：模块 RVA `0x4c46a0`
- `OBJECT_INFO.time`：模块 RVA `0x4c46b0`
- `SCENE_INFO` 基址：模块 RVA `0x4c4688`

调用者 RVA `0x209990` 在 `0x209a90`/`0x209a97` 用 RIP 相对 `lea` 把这两个模块内地址作为 `R9`/`R8` 传入。因此这里不是每次在插件栈上新建 SDK 结构，而是主程序在调用滤镜前覆盖一组共享结构。

### 实际参数（Windows x64 ABI）

`ObjectInfoPopulateCandidate_A` 的语义只按证据描述：

| 寄存器 | 实测用途 |
| --- | --- |
| `RCX` | 所有者/滤镜调度上下文，准确类型未知 |
| `RDX` | 内部对象求值状态 |
| `R8` | `SCENE_INFO` 输出地址 |
| `R9` | `OBJECT_INFO` 输出地址 |

动态命中时 `R11` 保存 `R9`，并实际指向 `OBJECT_INFO`。在帧 5 的有效样本中：

- 内部状态 `+0x0c` = `5`
- 内部状态 `+0x10` = `5.0`
- 对象记录 `+0x48` = `0`
- 内部局部偏移 `+0x28` = `0`
- 场景内部配置 `+0x74` = `30`，`+0x78` = `1`
- SDK 结果：`frame=5`，`time=0.16666666666666666`

### 精确计算

反汇编得到的当前构建公式为：

```text
OBJECT_INFO.frame =
    int32(state[0x0c])
    - int32(object_record[0x48])
    - int32(state[0x28])

OBJECT_INFO.time =
    (double(state[0x10])
     - int32(object_record[0x48])
     - int32(state[0x28]))
    * int32(scene_state[0x78])
    / int32(scene_state[0x74])

OBJECT_INFO.frame_total =
    int32(object_record[0x4c])
    - int32(object_record[0x48])
    - int32(state[0x28])
    + 1

OBJECT_INFO.time_total =
    OBJECT_INFO.frame_total
    * int32(scene_state[0x78])
    / int32(scene_state[0x74])
```

关键点是 `frame` 与 `time` 使用两个不同的源字段。`time` 没有读取刚写入的 SDK `frame`。

另一个实测样本也验证了对象局部换算：全局内部坐标 `178/178.0`，对象起点 `153`，结果为 `frame=25`、`time=25/30=0.83333333333333337`。

## 调用链与线程顺序

当前预览渲染样本中的已证实链为：

```text
通用渲染任务线程入口
  RVA 0x2cf2f4
  -> RVA 0x2a9353
  -> RVA 0x2a8ce9
  -> RVA 0x2647db
  -> RVA 0x26375f（0x26375c 的虚调用返回点）
  -> FilterDispatchCandidate_B, RVA 0x209990
       -> call ObjectInfoPopulateCandidate_A at RVA 0x209aa4
            -> OBJECT_INFO.time write at RVA 0x20954a
       -> 后续滤镜回调
```

有效初始样本：

- 写断点：QPC `190023399504`，线程 `27020`
- 探针回调：QPC `190023468290`，线程 `27020`
- QPC 频率：`10,000,000 Hz`
- 写入发生在回调前 `68,786` tick，即约 `6.8786 ms`

加入“对全部线程重新布置多个断点”的后续实验会人为增加几十毫秒，因此只用它确认先后顺序，不把延迟当作正常渲染性能。

本轮触发来自 AviUtl2 预览的单帧前进按钮，没有同时命中公开输出桥 `func_get_video`。因此可以确定的是“写入发生在对应 `func_proc_video` 之前、且在同一渲染线程”；相对于输出插件 `func_get_video` 入口/返回的精确时序尚未动态闭环。要闭环需在一次实际输出中同时跟踪 Phase 2 已识别的 getVideo 桥 RVA `0x22a6c0`，本文不把预览结果冒充输出结果。

## 上游 double 状态写入

### 动态命中的状态复制

预先观察内部 `double` 地址后，在生产者线程命中：

- 实际写指令：RVA `0x145f49`
  - `mov qword ptr [rcx+10h], rax`
- 异常 RIP：RVA `0x145f4d`
- 线程：`35392`（与滤镜回调线程 `38552` 不同）
- `RCX`：复制目标状态 `0x1dfea1143d8`
- `RDX`：复制源状态 `0x1dfea113f58`
- `RAX=0x4014000000000000`，按 IEEE-754 `double` 是 `5.0`

RVA `0x145f30` 开始逐字段复制内部状态：`+0x0c` 的整数和 `+0x10` 的 8 字节值都被原样复制。动态顺序为：

```text
QPC 203998906438  producer thread 35392  internal state copy (+0x10 = 5.0)
QPC 203999546090  render thread   38552  FilterDispatchCandidate_B entry
QPC 204000058539  render thread   38552  OBJECT_INFO.time write
QPC 204000559364  render thread   38552  probe func_proc_video (frame 5)
```

该命中本身是复制赋值，不是最初的算术生成点。它证明 `double` 是内部求值状态的一等字段，并跨生产/消费线程传递。

### 最初构造点：TimelineStateBuildCandidate_C

继续反汇编状态生产链后，RVA `0x2662d0` 的函数构造同一布局的内部求值状态。其起始路径明确执行：

```asm
; internal state begins at [rbp+0x170]
RVA 0x2663ad  mov       [rbp+0x17c], r8d       ; state +0x0c integer
RVA 0x2663b8  vcvtsi2sd xmm0, xmm0, r8d
RVA 0x2663bd  vmovsd    [rbp+0x180], xmm0      ; state +0x10 double
```

所以本次普通路径的初值确实是：

```text
state.double_position = double(integer_input_frame)
```

但是同一函数还存在另一个私有路径。它读取一个布尔栈参数和随后的 `double` 栈参数，并执行：

```asm
RVA 0x266561  vcvttsd2si eax, xmm0             ; double -> truncated integer
RVA 0x266565  mov       [rbp+0x17c], eax       ; state +0x0c
RVA 0x26656b  vmovsd    [rbp+0x180], xmm0      ; state +0x10, fraction preserved
```

后续 RVA `0x2665f5` 至 `0x26663a` 还能用 `double` 做边界裁剪，并把整数与小数余量分别保存。这是明确的子帧表达能力，不是仅用于显示秒数的 `frame/fps` 派生字段。

当前普通调用者 RVA `0x265590` 在调用该函数前把私有布尔参数设为 `false`、`double` 参数设为 `0.0`，因此实测预览/公开帧路径仍走整数初始化。尚未确认 AviUtl2 自带哪条功能会把该布尔参数设为 `true`，也尚未证明所有动画、缓存和特效都接受该私有 `double` 路径。

## 对问题的逐项回答

1. **谁初始化 `OBJECT_INFO`？**  
   AviUtl2 主程序的 `ObjectInfoPopulateCandidate_A`（RVA `0x209480`）在滤镜调度前覆盖模块内共享 `OBJECT_INFO` 暂存区。不是输出插件或探针初始化。

2. **谁填写 `object.frame`？**  
   同一函数在 RVA `0x209506` 写入，来源是内部整数坐标减对象起点和局部偏移。

3. **谁填写 `object.time`？**  
   同一函数在 RVA `0x20954a` 写入，来源是独立的内部 `double` 坐标，经对象局部偏移和场景 `scale/rate` 换算为秒。

4. **`time` 是 `frame/fps` 还是另一内部时间？**  
   直接写入不是从 SDK `frame` 计算，而是从内部 `double` 坐标计算。普通路径中该 `double` 最初等于整数输入帧，所以数值上表现为 `frame/fps`；内部另有可保留小数的私有路径。

5. **写入链收到什么时间类型？**  
   `ObjectInfoPopulateCandidate_A` 收到含整数和 `double` 两套坐标的状态对象，并读取整数 `rate/scale`。更早的 `TimelineStateBuildCandidate_C` 同时具有整数输入和私有 `double` 输入分支。没有看到专门的公开 timestamp 类型；有明确的 `double` 帧坐标和有理数场景帧率。

6. **线程与可重入性？**  
   状态生产/复制和滤镜消费可发生在不同线程；SDK `OBJECT_INFO` 又是模块内共享暂存区。不能假定单线程或可重入。任何 Hook 必须按调用实例保存/恢复，并考虑并发渲染。

7. **理论上能否修改？**  
   技术上可在 RVA `0x20954a` 后修改共享 `OBJECT_INFO.time`，但该点已经进入滤镜回调准备阶段，很可能只改变插件看到的报告值，无法倒推已完成的对象动画/特效求值。更有价值的候选是 RVA `0x2662d0` 内部状态的 `double +0x10` 构造/选择过程。

## `original_time + 0.001` 实验

本轮**没有执行**该写内存实验。

理由不是把未知结果当作失败，而是当前证据已经显示 `OBJECT_INFO.time` 在对象求值状态形成后才被复制到共享 SDK 暂存区；在这里加 `0.001` 只能可靠验证“滤镜回调能看到被改过的报告值”。当前测试对象也没有已校准、可测量的时间动画，无法据此判断动画状态是否改变。加上共享暂存区和多线程风险，这一实验目前不能安全地产生用户要求的因果结论。

如果后续执行，应先建立有确定解析公式的动画参数与像素/hash 基线，并分别做：

1. 只改 RVA `0x20954a` 后的 SDK 报告值；
2. 在 RVA `0x2662d0` 的 `double` 状态形成后、对象求值前改同样的 `+0.001 * rate/scale` 帧坐标；

两者对比才能区分“报告字段”与“真正控制求值的时间”。

## Hook 可行性判断与风险

Phase 3 的核心问题得到积极结果：AviUtl2 内部存在一个独立 `double` 帧坐标，且内部状态构造函数能保留小数。项目不需要仅因为 `OBJECT_INFO.time` 而立即判定为必须修改本体的 D 级；仍有 C 级内部 Hook 的技术可能性。

当前最早、最有价值的候选是 `TimelineStateBuildCandidate_C`（RVA `0x2662d0`），而不是：

- RVA `0x20954a`：太晚，只是 SDK 报告字段；
- RVA `0x145f49`：只是状态复制，会同时影响生产/消费与对象生命周期；
- RVA `0x209990`：滤镜调度入口，内部时间已确定。

尚需在进入最小 Hook 原型前完成：

- 找到把 `TimelineStateBuildCandidate_C` 私有布尔参数设为 `true` 的真实调用者；
- 验证该 `double` 在动画插值、脚本、场景合成之前生效；
- 追踪缓存键是否仍仅使用 `state+0x0c` 整数；
- 对实际 `func_get_video` 输出调用做同步动态闭环；
- 验证音频是否有相同的双坐标时间系统；
- 验证主线程依赖、并发与重入。

因此本阶段没有制作时间修改 Hook，也没有写死绝对地址。

## 调试观察器稳定性说明

观察器开发过程中，执行断点曾因未设置 Resume Flag 而在同一 RIP 重复命中；另一次脱离时把旧的 DR1 上下文写回当前线程。两个测试用 AviUtl2 进程因此退出。两次都没有修改 `OBJECT_INFO.time`、项目文件或图像数据；测试工程从 AviUtl2 的异常备份恢复。

当前源码已经：

- 在执行断点继续时设置 RF；
- 脱离前清空目标所有线程的 DR0-DR3、DR6、DR7；
- 在重新布置断点后重新读取当前线程上下文，避免恢复旧 DR1。

修正后的观察器已重新编译，但最后一项脱离修正没有再对目标进程做破坏性复测。因此它仍应视为一次性研究工具，不应嵌入生产插件或用于有未保存工作的 AviUtl2 实例。
