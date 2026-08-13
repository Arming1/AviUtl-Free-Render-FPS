# Phase 2 运行时探针结果

## 结论

在 AviUtl2 v2.1.4 上实际运行探针后，确认输出插件可以按任意顺序请求有效的**整数帧**。重复请求同一整数帧时，AviUtl2 会再次执行至少该帧中被观察对象的滤镜求值回调，而不是只把上次的输出插件缓冲区指针原样返回。

本次运行没有观察到输出 API 提供时间戳、子帧索引或可写的场景时间。对象滤镜回调虽然收到 `double time`，但实测它等于对象局部整数帧除以场景帧率；这不能证明渲染器能接受任意子帧时间。

## 测试环境

- 主程序：`aviutl2_v2.1.4/aviutl2.exe`
- SHA-256：`ED8AA51A80017839C232F35E7D3F6CB5E56FD09E8E13604726119CFB7C67CE89`
- 架构：x64 PE32+
- 工程文件：`experiments/timeline_eval_probe/runtime_probe_30fps.aup2`
- 工程/输出速率：`rate=30`、`scale=1`，即 30 FPS
- `OUTPUT_INFO.n`：321 帧
- 场景：1920×1080、44.1 kHz；图形对象位于场景绝对帧 240–320，在对象局部 0–80 帧间作线性位移
- 输出探针：`experiments/render_probe/render_probe.cpp`，版本 3
- 对象求值观察探针：`experiments/timeline_eval_probe/timeline_eval_probe.cpp`，版本 1
- 完整原始日志：
  - `experiments/render_probe/runtime_probe_30fps_v3.log`
  - `experiments/timeline_eval_probe/timeline_eval_probe_runtime_v3.log`

观察探针是无图像读写的公开滤镜插件：回调直接返回 `true`，只记录 `SCENE_INFO`、`OBJECT_INFO`、QPC 与线程 ID。它不是内部 Hook，也不修改时间或画面。

## 实际请求顺序与结果

输出回调实际请求顺序为：

```text
0 -> 320 -> 160 -> 1 -> 320（重复 request_order=1）
```

| request_order | 整数帧 | 探针计算的隐含时间 | 结果 | 整帧 FNV-1a | 缓冲区 |
|---:|---:|---:|---|---|---|
| 0 | 0 | 0 s | non-null | `122a73791dee9325` | `000001FD61552060` |
| 1 | 320 | 10.666666666667 s | non-null | `5fc6a0a5af83526b` | 同上 |
| 2 | 160 | 5.333333333333 s | non-null | `122a73791dee9325` | 同上 |
| 3 | 1 | 0.033333333333 s | non-null | `122a73791dee9325` | 同上 |
| 4 | 320（重复） | 10.666666666667 s | non-null | `5fc6a0a5af83526b` | 同上 |

`implied_timestamp_seconds` 是探针按 `frame * scale / rate` 计算的说明字段，不是 AviUtl2 传入的时间戳。

帧 320 的哈希与无对象的帧不同，两次帧 320 的整帧哈希相同。这说明非顺序请求确实取得了目标整数帧的不同画面，重复请求则得到相同画面内容。所有调用返回相同地址，证明该地址是可复用的宿主缓冲区，不能用指针是否变化判断是否重渲染。

## 重复请求是否重新求值

帧 320 的两次输出请求分别对应下面两次对象滤镜回调：

| 输出请求 | 请求 begin QPC | 对象回调 QPC | 请求 end QPC | 对象回调值 |
|---|---:|---:|---:|---|
| 第一次 frame 320 | 128911109883 | 128911112189 | 128911198311 | `frame=80, time=2.6666666666666665` |
| 重复 frame 320 | 128911519279 | 128911522219 | 128911611871 | `frame=80, time=2.6666666666666665` |

两次对象回调都位于各自的 `func_get_video(320, ...)` 调用区间内。因此可以确认：重复请求帧 320 时，至少对象的滤镜/效果求值链再次执行。

不能仅凭本探针确认所有上游动画、所有特效或所有缓存层都重新计算；某些内部节点仍可能命中缓存。

## 时间值的实际含义

帧 320 时观察到：

```text
scene_rate=30
scene_scale=1
frame_s=240
frame_e=320
object.frame=80
object.frame_total=81
object.time=2.6666666666666665
object.time_total=2.7000000000000002
```

这里 `OBJECT_INFO.frame` 和 `OBJECT_INFO.time` 是**对象局部**位置：

```text
object.frame = output_frame - frame_s = 320 - 240 = 80
object.time  = object.frame / 30 = 2.6666666666666665 s
```

因此，滤镜边界确实存在双精度秒值，但本次值与整数局部帧严格一致。没有观察到 `80.5` 帧、`1/60` 秒或其他子帧状态。

SDK 对应定义是 `reference/aviutl2_sdk/filter2.h:320` 的 `SCENE_INFO`、`:328` 的 `OBJECT_INFO`、`:331-334` 的整数帧与双精度秒字段，以及 `:383-388` 的 `FILTER_PROC_VIDEO::scene/object`。这些指针均为 `const`，观察探针没有公开写入时间的能力。

## 回调顺序与线程

本次输出的顺序是：

```text
输出插件 func_output（thread 23904）
  -> video_request_begin（thread 23904）
     -> AviUtl2 内部渲染
        -> 对象 func_proc_video（thread 29360）
     <- video_request_end（thread 23904）
<- output_callback_end（thread 23904）
```

所有输出插件事件都在 thread 23904。对象滤镜回调在 thread 29360。SDK 也在 `reference/aviutl2_sdk/filter2.h:839-855` 明确把图像/音频滤镜处理描述为独立线程执行。由此可知，未来 Hook 若使用全局“当前时间”变量，至少必须处理跨线程可见性、嵌套渲染和并发恢复，不能假定整个调用链在输出线程同步执行。

## 对用户指定问题的实测回答

- 工程 FPS：实际为 30/1。
- 工程总帧数：本次宿主提供的输出范围为 321 帧。公开结构只保证这是 `OUTPUT_INFO.n`；本探针没有推测工程长度公式。
- 请求帧顺序：`0, 320, 160, 1, 320`。
- 非顺序帧请求：全部返回 non-null；末帧画面哈希与空帧不同。
- 重复请求同一帧：再次进入对象滤镜回调，画面内容相同，宿主缓冲区地址复用。
- 时间戳/子帧信息：输出回调没有；对象滤镜中仅观察到由整数对象帧导出的 `double time`。
- 回调调用顺序：对象滤镜回调位于 `func_get_video` 的 begin/end 之间。
- 主线程依赖：未确认。只确认输出回调线程与对象滤镜线程不同。
- 音频：本阶段运行时探针未请求音频，不能据此推断音频同步行为。

## 未被本次实测证明的事项

- AviUtl2 是否存在未公开、可传任意时间戳的场景渲染入口。
- `OBJECT_INFO.time` 的构造函数或写入地址。
- 动画插值核心究竟以帧、秒还是其他内部时间基为主键。
- 内部缓存是否全部以整数帧为 key。
- 场景渲染函数是否可重入、是否必须在某个特定线程运行。
- 音频和视频是否共享同一个全局时间状态。

以上事项必须通过针对 `OBJECT_INFO` 构造点的动态断点/写入监视继续验证，不能从当前日志外推。
