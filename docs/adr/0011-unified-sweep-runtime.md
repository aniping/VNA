# 采用统一 SweepRuntime 与完整/预览双显示通道

## 决策

Continuous、Single、Hold 和 Restart 共用一个 `SweepRuntime`。它是唯一
`RawSweepSource` 所有者，使用一个采集 worker，并在一次 Sweep 的安全边界切换
完整不可变计划。Channel 持有 Continuous/Single、Trigger Source 和 Sweep 参数
等配置；Hold、Preparing、Sweeping、Publishing、Failed 等运行状态由
`SweepRuntime` 持有，两者不得互相冒充。

配置事务先在可见状态之外编译候选计划，再原子提交 Channel 配置、新
`stateRevision` 和完整 pending plan。已经开始的 Sweep 使用启动时捕获的
applied plan、revision 和频率轴；下一边界只采用最新完整 pending plan，避免
把一帧拼成多个配置版本，也不需要停止并重建采集 owner。

Restart 请求取消当前 SweepId。只有数据源确认停止、资源释放且旧 Sweep 不再能
发布迟到数据后，同一个 worker 才使用最新计划创建新 SweepId。切换到 Single
允许当前 Continuous Sweep 在边界完成后进入 Hold；Single 的 `Restart Sweep`
也由同一运行时调度，不恢复或旁路使用旧的独立 SingleSweepExecutor。

## 两种显示提交语义

- `LastCompleteFrameSet` 是完整、校验通过并原子提交的业务结果。它进入正式
  FrameRepository，可被 Query、SCPI、文件和重连 retained latest 使用。
- `InProgressSweep` 是当前 Sweep 已真实采集并经后端投影的暂态 Preview。它只
  通过 latest-only 实时通道发送，不进入 Repository，不可查询、记录或用于
  Marker/Limit，也不参与 Operation 完成判断。

ADR-0009 的“一个 raw frame 派生全部目标、完整 FrameSet 全有或全无”继续约束
最终结果。Preview 不是部分 FrameSet 的仓库存档，而是独立的暂态观察通道。
服务端保留 `lastComplete` 与 `currentPartial` 两份状态；前端只能按经 ZNB 实机
确认的绘制策略组合它们，不能覆盖、插值或假造业务数据。

一次 Sweep 只有在采集结束、完整性校验、Measurement 合成、Trace 投影以及完整
FrameSet 发布全部完成后才结束。Single 或 Restart 的 Operation 也只有在这个
提交点之后才能进入 Succeeded。预览到达、最后一个 raw chunk 到达或页面绘制
完成都不是 Operation 成功条件。同一 generation 的失败与取消保留上一完整结果；
generation 变化仍按 ADR-0009 原子失效旧完整 FrameSet 和当前 Preview。

多轮 Single Operation 可以在 Sweep 边界采用更新后的配置。其
`submittedAtStateRevision` 只记录请求准入时的 revision；每轮 FrameSet 自身的
`stateRevision` 与 generation 才描述实际数据配置，不能由 Operation 字段代替。

## 仿真与因果边界

Simulation 仍按 ADR-0003 只生成 Raw Receiver 数据。仿真总 Sweep 时长和 chunk
粒度可以通过适配器/测试配置调整；它们只改变数据交付时序，不是 Channel 配置，
不改变 `stateRevision` 或样本值。chunk 由 acquisition seam 产生，并携带足以
校验 SweepId、源状态、频点范围与顺序的身份；前端不得通过计时器制造逐点动画。

完整结果与 Preview 都必须能关联 ChannelId、SweepId、stateRevision、配置
generation 和实际频率轴。新 generation、新 SweepId、Restart 或失败会废弃旧
currentPartial；旧消息在提交/发送边界被拒绝，不能覆盖新代或 lastComplete。

## 取舍与后果

选择双通道而不是把 partial 写入正式 Repository，是为了同时满足 ZNB 的扫频中
可见更新、SCPI/文件所需的完整结果以及 ADR-0008 的确定完成栅栏。代价是运行时、
协议和前端必须显式处理 Preview 生命周期与背压，但它避免把不完整数据误当成
可查询真值，也避免为 Continuous 和 Single 维护两套硬件调度。

ZNB v74 没有明确默认 Alternated、多 S 参数场景中未采集点与上一完整曲线的精确
合成方式。本决策只要求保留可区分的 `lastComplete/currentPartial`，最终视觉策略
必须经 ZNB 实机或用户截图确认，不能由实现便利性反推。

Trigger 首期只支持 None；External、Manual、Multiple、Trigger Out、独立 Abort、
多 Channel 调度、SCPI、Segment/Power/CW/Time Sweep、平均、校准、Marker、
Record/Replay 和真实硬件不属于本决策的实现范围。

## 与既有决策的关系

本 ADR 只在采集所有权和运行控制范围内演进 ADR-0009：其中名为
`ContinuousAcquisition` 的唯一 source owner 将被扩展/替换为统一 `SweepRuntime`，
而不是与 `SweepRuntime` 并存。ADR-0009 的 Sij 端口语义、一次 raw 派生全部目标、
完整 FrameSet 全有或全无、generation 失效和同代 last-complete 语义继续有效。

ADR-0003 的 Simulation raw payload 边界和 ADR-0008 的完整发布后 Operation 完成
条件不变。
