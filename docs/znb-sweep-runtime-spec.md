# ZNB 单通道 Sweep 运行与逐步显示规格

> 状态：运行与数据架构合同已冻结；数值 Capability 与未采集点视觉仍待门禁
>
> 基线：`main@d2dbc87`

## 1. 用户可见目标

程序仍以 Channel1、Continuous 和 Trigger None 启动。用户修改当前 Channel 的
扫频参数后，正在进行的 Sweep 不被新配置污染，下一安全 Sweep 边界采用完整的
新计划。Continuous、Single、Hold 和 Restart 共用唯一采集运行时与唯一原始数据
源，不启动第二个 worker，也不因浏览器连接状态改变采集。

曲线在一次 Sweep 进行期间能够随真实采集进度逐点或分块形成。该效果必须来自
采集端交付的原始接收机 chunk，经后端完成 S 参数和 Trace 格式投影后送达页面；
前端不得用定时器、插值或重复旧值伪造扫描动画。只有完整 FrameSet 提交成功才
代表一次 Sweep 完成，也才允许相关 Operation 成功。

## 2. ZNB 依据与入口

| 行为 | ZNB v74 章节 | 印刷页 | PDF 页 | 本切片入口 |
| --- | --- | ---: | ---: | --- |
| 工具栏 Restart | 3.3.2.2 Toolbar | 45–46 | 45–46 | 工具栏 Restart |
| Sweep 准备、partial measurement 与驱动方式 | 4.1.4 Sweep control | 78–81 | 78–81 | Channel → Sweep |
| 数据 Trace 随测量更新 | 4.2.1.2 Traces | 89–91 | 89–91 | Diagram 数据 Trace |
| Start/Stop/Center/Span | 5.8 Stimulus softtool | 523–525 | 523–525 | Stimulus softtool |
| Power 与 IF Bandwidth | 5.9.1 Source Power / 5.9.3 Bandwidth | 528–531 | 528–531 | Power Bw Avg softtool |
| Points 与 Sweep 参数 | 5.10.1 Sweep Params tab | 533–536 | 533–536 | Sweep softtool |
| Trigger None / Free Run | 5.10.3.1 Controls on the Trigger In tab | 550–552 | 550–552 | Trigger In softtool |
| Continuous、Single、Sweeps、Restart | 5.10.5.1 Controls on the Sweep Control tab | 556–559 | 556–559 | Sweep Control softtool |
| Restart 保留旧数据的 Factory 行为 | 5.23 Setup softtool / User Interface tab；SYSTem:TRESet | 813–814、1582 | 813–814、1582 | Restart 设置 |
| IFBW、Sweep count、mode、points 的远程合同 | 7.3.14.2 BANDwidth；SWEep 命令 | 1307、1469、1473 | 1307、1469、1473 | 本地入口的统一业务语义 |

ZNB 第 79 页明确区分两种驱动方式：Chopped 可逐点形成 Trace；默认 Alternated
会等待最后一个 partial measurement，但第 80 页又允许在最后一个 partial
measurement 的首批数据到达后开始处理。因此本规格冻结“运行中数据必须有正式
通道”，但不臆测默认设备对尚未采集点的最终绘制细节。

## 3. 配置状态与运行状态

Channel 配置只公开：

- `SweepMode::Continuous`：一轮完成后立即开始下一轮，是 Factory Preset 默认值。
- `SweepMode::Single`：只运行配置的 Sweep 数量，完成后进入 Hold。
- `TriggerSource::None`：首期唯一支持的触发源；页面文字为 `None`。
- `sweepCount`：Single 的轮数，默认 1，ZNB 范围 1..100000；Continuous 下控件
  保持可见但禁用。

`Hold` 不是第三个 `SweepMode`，而是 `Single + 没有排队或运行中的 Sweep` 的运行
状态。运行态不得反写为 Channel 配置，也不得把 raw sequence、frame、source
状态放进 `StateSnapshot`。

运行时至少可观察以下阶段：

```text
Idle(Hold) → Preparing → Sweeping → Calculation → Idle(Hold)
                      ↘ Failed / Canceled ↗
```

Continuous 在完整提交后继续 Preparing；Single 在指定轮数全部完成后进入 Hold。
页面按 ZNB 第 558 页显示 Hold、Preparing、Sweeping、Calculation 与 Failed；
内部 Publishing 归入 Calculation，不得成为公开状态或另一套状态机。

## 4. 统一运行控制

系统只有一个 `SweepRuntime`，它独占 `RawSweepSource` 和采集 worker，并负责
Continuous 与 Single 的调度、Restart、取消确认、计划切换和 SweepId 生命周期。
协议、CommandBus、前端和 Simulation 均不得直接启动另一数据源。

### 4.1 Continuous 与 Single

- Factory Preset 在 revision 0 以 Continuous + None 自动采集，无需用户点击。
- 从 Continuous 选择 Single 不取消当前 Sweep；当前 Sweep 使用既有不可变计划
  完成后不再自动续扫，运行时进入 Hold。
- Single Hold 下的动作文字仍为 ZNB 的 `Restart Sweep`。一次动作创建一个
  Operation，运行 `sweepCount` 轮；每轮完整结果都可发布，第 `sweepCount` 轮
  完整发布后 Operation 才成功并回到 Hold。
- Single Operation 运行时再次执行 `Restart Sweep`，按统一 Restart 规则取消旧
  SweepId 并从第一轮重新开始，不形成第二个在途 Single 请求。
- 切回 Continuous 后由同一 worker 在安全边界恢复自动续扫，不创建 Operation。

### 4.2 Restart

- Continuous 下 Restart 可用；它请求终止当前测量周期，并用最新已提交计划开始
  新 Sweep。
- Restart 为长操作并返回 OperationId。旧 Sweep 只有在数据源停止、资源释放且
  不能再发布迟到 chunk 后才进入 Canceled；随后新 Sweep 才能开始。
- 每次新 Sweep 使用新 SweepId。任何旧 SweepId 的迟到 chunk、预览或完整结果
  均被拒绝，不能污染新 Sweep。
- Restart 不修改 Channel 配置，不增加 `stateRevision`，也不清除上一完整曲线。
  这遵循第 814 页 Factory 默认关闭“Restart: Set all Traces to 0”的行为。
- Single Hold 使用同一个 `Restart Sweep`，不创造手册不存在的 `Start Sweep`
  文案，也不把 Restart 伪装成第二套单次采集 worker。

本切片不提供独立 Abort/Stop 命令。Restart 所需的取消是 `SweepRuntime` 内部的
受控状态转换，不提前开放完整 Abort 产品语义。

## 5. 动态 Sweep 计划

Start、Stop、Center、Span、Points、IF Bandwidth 和 Power 都属于 Channel 配置。
耦合、范围、单位和能力校验由业务核心负责；前端只提交用户意图和显示权威状态，
不得自行计算 Center/Span 与 Start/Stop 的最终业务值。

命令先在可见状态之外完成输入校验、能力校验和不可变候选计划编译；只有全部
成功后，才在同一个线性化提交中原子公开 Channel 配置、新 `stateRevision` 与
完整 pending plan。当前 Sweep 始终继续使用开始时捕获的 plan、stateRevision
和频率轴；pending plan 只能在下一安全 Sweep 边界整体替换 applied plan。

边界规则如下：

- 同一 Sweep 期间多次成功配置以最新完整 pending plan 为准，不混合字段。
- 非法输入、能力不支持或计划编译失败不改变配置、revision 或 pending plan。
- 配置命令成功不表示新计划已经采集；运行查询必须区分 configured revision 与
  applied revision。
- 新计划首次开始时产生新 SweepId；旧计划的完成帧保留其原 stateRevision。
- 当前阶段仍使用线性频率 Sweep，不新增 Segment、Power、CW 或 Time Sweep。

已确认的输入合同只有：Single `sweepCount` 为 1..100000、默认 1（第 1469 页）；
ZNB 频率 Sweep 的 Points 为 1..100001（第 534、1473 页）；基础 IF Bandwidth
为 1 Hz..1 MHz，并按 `1/1.5/2/3/5/7 × 10^n Hz` 向上取允许值，10 MHz 属于
K17 选件（第 531、1307 页）。首期不启用该选件。

这里存在两个编码前门禁，未裁决前不得宣称参数功能符合 ZNB：

- 现有完整帧合同是 2..2048 点，与 ZNB 的 1..100001 不同；需要用户确认首期
  Capability 限制及页面反馈，不能静默把当前容量当成 ZNB 原厂范围。
- 频率与 Power 范围依具体 ZNB 型号、选件和端口能力而变；尚未冻结目标型号的
  Capability 数值。现有 Preset 值不能反推全系列 ZNB 的输入范围。

## 6. 完整结果与运行中预览

### 6.1 LastCompleteFrameSet

- 来自一个 Sweep 的完整、校验通过且原子提交的全部 Trace 结果。
- 进入正式 FrameRepository，可被 Query、SCPI、文件、重连 retained latest 和
  后续分析读取。
- 只有该提交完成后，一次 Sweep 才完成；Single/Restart Operation 才可成功。
- 同一 generation 内的新 Sweep 失败、取消或预览中断时保留上一份完整结果。
  generation 实际变化时按 ADR-0009 先失效旧完整 FrameSet，不能跨代保留。

### 6.2 InProgressSweep

- 只表示当前 Sweep 已真实取得并完成后端投影的部分点，携带 SweepId、ChannelId、
  stateRevision、配置 generation、频率区间、已完成点范围和总点数。
- acquisition 交付按源状态和连续频点范围标识的 raw chunk；数据面按当前计划从
  chunk 合成 Measurement 并投影 Trace Preview，前端不接触 raw receiver 数据。
- Preview 只走 latest-only 实时推送；慢客户端可以丢过时 Preview。
- 权威完整 Sweep 进度以 `points × sourceStates` 为分母，以已校验 raw range
  样本数为分子；All-S 与多 Trace 复用同一采集工作，不重复增加分母。
- 设置导致 material generation 变化后，首轮标记从安全边界应用开始，直到该代
  首个完整 FrameSet 成功发布才清除；Scale 与 Restart 不单独置位。
- Preview 不进入 FrameRepository，不可 REST/SCPI 查询、不可记录，不参与文件保存、
  Marker、Limit 或 Operation 完成判断，也不写逐 chunk 运行日志。
- Restart、失败、连接到新 SweepId 或 generation 变化时立即废弃旧 currentPartial；
  迟到消息按 SweepId、revision 和 generation 拒绝。
- ADR-0009 的完整 FrameSet 全有或全无仍然成立；Preview 是独立暂态通道，不能
  被提升或拼接为伪完整 FrameSet。

服务端必须在同一 generation 内同时保留 `lastComplete` 与 `currentPartial`。
generation 变化会同时清除二者；Sweep 参数带来的 stateRevision/频率轴变化不
得把旧 revision 的 lastComplete 重标为新配置。页面基于两者绘制，但
不得把二者合并成新的业务真值。浏览器断开只丢失暂态 Preview，不停止 Sweep；
重连先取得 retained `lastComplete`，再接收当时仍有效的最新 Preview。

## 7. 未采集点视觉决策

ZNB 手册足以证明 Trace 会随采集更新，却没有明确当前默认 Alternated、多 S 参数
场景下每个未采集点究竟显示上一完整值、留空，还是以其他样式区分；Restart 后
已经画出的 partial 如何过渡也未确认。

因此实现必须先保留两份可区分的数据：

- `lastComplete`：上一轮完整曲线；
- `currentPartial`：本轮已完成的真实点及其有效范围。

前端在编码前通过 ZNB 实机或用户截图确认以下三选一策略，不得猜测：

1. 未采集区继续显示 lastComplete，已采集区覆盖为 currentPartial；
2. 只显示 currentPartial，未采集区留空；
3. 两者以 ZNB 的其他明确视觉规则同时呈现。

该决策只改变呈现合成，不改变双通道所有权、Sweep 完成条件或后端投影职责。

## 8. Simulation 与可测试时间

Simulation Backend 继续只产生 RawReceiver 数据。它接受显式的仿真扫频总时长
和 chunk 粒度配置，以可控节奏从 acquisition seam 交付 chunk；配置属于仿真
适配器/测试环境，不是 Channel 业务状态，也不增加 `stateRevision`。

- 相同 seed、计划、SweepId 与 chunk 划分必须得到相同样本和最终完整帧。
- 调整总时长只改变交付时序，不改变频率轴、样本值或完整结果。
- 自动测试通过受控时钟、门闩或显式推进接口观察中间状态，不以不可靠 sleep
  推测进度。
- 正式 Windows release 验收可使用较长仿真时长目视曲线形成过程；设置入口是
  部署适配选择，不得伪装成 ZNB Channel 参数。

## 9. 失败与恢复

- chunk 次序错误、重叠、越界、非有限值、源状态缺失或最终点数不完整会使当前
  Sweep 失败，不发布完整 FrameSet，也不覆盖 lastComplete。
- Continuous 的单轮失败保留 lastComplete、丢弃 currentPartial，并继续用最新
  有效计划尝试下一轮；运行状态和错误可查询，但不制造无限 Operation。
- Single 任一轮失败使 Operation 进入 Failed，并回到 Hold；已完成轮的完整帧
  仍是真实结果，但不能把部分完成的计数宣称为整个 Operation 成功。
- Restart 取消使旧 Operation（若有）在资源真实释放后进入 Canceled；新 Restart
  Operation 只在新 Sweep 的完整 FrameSet 提交后成功。
- 日志故障、WebSocket 断线或无浏览器连接不得改变上述业务结果。

## 10. 明确排除

本切片不实现 External、Manual、Multiple、Timer 等 Trigger Source，也不实现
Trigger Out、SCPI、Restart Manager、全 Channel Restart、独立 Abort、平均、
校准、Marker、Segment/Power/CW/Time Sweep、多 Channel 调度、Record/Replay
或真实硬件。Trigger 菜单中除 None 外的能力必须隐藏或原生禁用，不能发送请求。

本切片不把 Preview 用作历史缓存、数据导出、分析输入或断线补发，也不新增第二
采集 source、第二 worker、按 Trace 扫描、raw JSON/WebSocket 或前端业务换算。

## 11. 验收门禁

自动化公共 seam 至少证明：

1. Preset 为 Continuous + None，唯一运行时在无浏览器时仍持续采集。
2. 配置在下一安全边界整体生效；旧 Sweep 的 revision、轴和样本不被新配置污染。
3. 连续多次配置只采用最新 pending plan；失败命令不改变 applied/pending 状态。
4. Single 运行准确轮数，完整发布后才成功并 Hold；Continuous 不创建永久 Operation。
5. Restart 取消旧 SweepId，等待资源释放后复用同一 source 开始新 Sweep；迟到数据
   和旧 Preview 被拒绝，lastComplete 保留。
6. 受控 chunk 逐步增加 currentPartial，最终结果与一次性交付的确定性金样一致；
   前端没有定时假动画或领域计算。
7. Preview latest-only、不可 Query/记录；完整 FrameSet 可 retained、查询和重连。
8. raw/chunk/合成/投影/发布任一步失败均不覆盖 lastComplete，Operation 进入确定终态。

Windows 正式验收只从 `release/VectorNetworkAnalyzer` 启动服务，在
`127.0.0.1:8080`、1280×800、100% 缩放下完成：

1. 冷启动无需点击即显示 Continuous、None 和持续刷新曲线。
2. 调长仿真 Sweep 时间后，目视确认曲线由真实点/块逐步形成，而非整帧跳变或
   前端动画；Network 只出现处理后的 Preview，不出现 raw receiver 数据。
3. Sweep 中修改频率、Points、IFBW 或 Power，确认本轮保持旧轴，下一轮整体采用
   新值，状态与帧 revision 可对应。
4. Continuous 下 Restart 可用，旧 partial 立即失效、上一完整曲线保留，新 Sweep
   完整后 Operation 成功。
5. Single 的 Sweeps 输入仅在 Single 启用；Restart Sweep 完成指定轮数后进入 Hold，
   不存在第二 worker/source。
6. 断开浏览器不停止采集；重开先显示 lastComplete，再接收当前最新 Preview。
7. 按第 7 节对未采集点和 Restart 过渡完成 ZNB 实机对照并记录最终视觉决策；
   未完成该项时只能标记“已实现待实机验收”，不得宣称符合。

本规格的自动测试不能替代正式 release 的真实浏览器目视、交互和网络证据。
