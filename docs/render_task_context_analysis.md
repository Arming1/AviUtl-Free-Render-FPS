# Render task context analysis（Phase 5）

## 结论

Thread-local storage 不足以把输出样本与场景求值关联。最终实验明确观察到三个不同线程：

| 阶段 | 线程 ID（最终运行） |
|---|---:|
| 输出插件 / `func_get_video` 调用与返回 | 29008 |
| RVA `0x2662d0` 时间轴状态构造 | 4536 |
| 滤镜回调 / `OBJECT_INFO` 观察 | 26472 |

一个只写在输出线程 TLS 的 `current_subframe_time` 不会自动出现在状态构造线程；一个只写在工作线程 TLS 的值也不能可靠覆盖后续滤镜线程、线程池复用、嵌套渲染或并发任务。

本 PoC 使用的是**有严格生命周期的单活动事务**，不是可扩展的全局变量。它证明了关联可做，但只在同步、无重叠输出调用的测试条件下安全。

## 实测时序

样本 0 的 QPC 时序（频率 10,000,000 Hz）：

```text
263447473400  output plugin request_begin, thread 29008
263447475613  aviutl2 output bridge entry, thread 29008
263447481694  timeline builder entry, thread 4536
263447482007  private input mapping applied, thread 4536
263447485750  filter callback sees OBJECT_INFO, thread 26472
263447552388  output bridge return, thread 29008
263447553782  output plugin request_end, thread 29008
```

来源：`phase5_schedule_output_final.log:2-3`、`phase5_schedule_watch_final.log:4-7`、`phase5_schedule_object_info_final.log:2`。

这证明公开 `func_get_video` 在调用者看来是同步的，但内部工作跨至少三个线程。回调返回是本次任务生命周期的可靠外边界；线程 ID 本身不是任务 ID。

## PoC 如何关联请求

1. 在输出桥 RVA `0x22a6c0` 的硬件执行断点读取整数请求帧、输出线程和栈上的动态返回地址。
2. 建立 `ActiveRequest{ordinal, requested_frame, target_coordinate, output_thread, return_address}`。
3. 只在活动事务存在时接受 RVA `0x2662d0` 命中；再验证直接调用者 RVA `0x2657e8` 和 `R8D==requested_frame`。
4. 在输出线程的动态返回地址设置第三个硬件执行断点；命中后销毁事务。
5. 若新输出请求在旧事务返回前进入、顺序/帧号异常、调用者不匹配或写回失败，则永久停用本轮修改。

代码见 `experiments/subframe_scheduler_test/subframe_scheduler_watch.cpp:292-450`。最终 63 个请求全部 `overlap=0`、`order_ok=1`，`mutation_allowed` 在脱离时仍为 1；见 `phase5_schedule_watch_final.log:4-253`。

这个设计比裸 `current_subframe_time` 多了任务边界和一致性守卫，但 `g_active` 本身仍是进程级单槽状态，只适合本次实验。它明确拒绝并发，而不是解决并发。

## 内部上下文候选

在 61 次实际状态构造命中中，入口：

- `RCX = 0x1a06abb8290`
- `RDX = 0x1a06a952740`

保持稳定；输出帧和目标 double 改变时，这两个指针不变。这说明存在长期 render/scene 相关上下文，但**不能据此把 RCX 或 RDX 命名为 RenderTask**：

- 它们可能是场景服务、对象描述或共享 worker context；
- 在单场景、单对象、单输出任务中保持不变并不能证明每任务唯一；
- 没有观察到其分配/释放、引用计数或跨线程所有权。

下一步应在输出桥进入时追踪这些指针从何处进入工作队列，并在状态构造、滤镜和返回点比较同一对象/令牌，而不是直接用地址作为生产键。

## TLS 是否足够

| 方案 | 结论 | 原因 |
|---|---|---|
| 输出线程 TLS | 不足 | 状态构造发生在线程 4536，不是输出线程 29008 |
| 状态构造线程 TLS | 不足 | 同一 worker 可能连续处理不同请求；滤镜又在线程 26472 |
| 全局单值 | 不足 | 并发、嵌套调用和预览/输出交错会串值 |
| 以整数帧为键的全局表 | 不足 | 重复帧不同 subframe 碰撞；缓存实验已实证 |
| 以调用栈返回地址为键 | 不足 | 同一插件调用点所有请求相同，只能定义生命周期边界 |
| render-task/token 关联表 | 最有希望 | 可在工作队列传播，并允许同帧多个 sample；仍需定位真实 token |

## 生产关联模型建议

```text
OutputSampleKey = (export_session_id, sample_index, output_rate/scale)
RenderBinding   = (internal_task_or_request_token -> OutputSampleKey)
TimelineCoord   = sample_index * project_rate * output_scale
                  / (output_rate * project_scale)
```

要求：

- 在输出桥创建不可变 sample descriptor；
- 在入队点把 descriptor 与内部任务对象/令牌绑定，而不是只靠线程；
- 状态构造时按任务令牌读取目标坐标；
- 任务完成/异常/取消时清除绑定；
- 支持引用计数、嵌套调用和同一任务的多个对象/效果求值；
- 把 cache identity 与相同 sample key 关联；
- 音频使用 export session 的同一有理时间基，但不要复用视频线程 TLS。

double 可用于 AviUtl2 的最终求值输入，但调度器内部应保存有理数 `(sample_index, rate, scale)`，只在调用边界转换为 double，避免长时项目的累计误差和 cache hash 不稳定。

## 安全与停止条件

最终 watcher：

- 附加后校验映像大小、三处字节签名；
- 对所有线程显式挂起、保存 DR0-DR7、设置后恢复；
- 拒绝已有调试寄存器占用；
- 使用 RF 防止执行断点立即重触发；
- 最后一个请求后恢复 141 个线程的保存状态并脱离；
- 最终日志中 `occupied=0`，AviUtl2 在导出完成后继续响应。

实现见 `subframe_scheduler_watch.cpp:150-275,470-606`。这些措施只降低调试 PoC 风险，不等同于生产 Hook 的异常安全和卸载安全。

## 尚未解决

- 真正的内部任务对象/队列节点位于哪里；
- 一个输出请求是否可能并行构造多个 scene state；
- 预览、缩略图、嵌套场景和第三方滤镜是否会重入同一链；
- GPU 提交完成是否晚于 `func_get_video` 返回；
- 音频 worker 的任务令牌是否能与视频 export session 关联。

