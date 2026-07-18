# VNA 商用功能逐项对齐矩阵

> 状态：176 项已完成官方证据归类；兼容目标、算法/计量、真实单板和目标平台门禁尚待闭合，因此尚未形成冻结产品规格。

本文是候选整体架构与商用 VNA 功能之间的逐项核对入口。它不以“文档提到过某个名词”作为覆盖完成，而要求每个功能项同时明确外部行为、产品等级、领域落点、控制入口、数据/状态语义、异常边界和可执行验收证据。跨功能的 Buffer 所有权、A/B/Stage/C 父引用、校准、Diagram、Query/Export 与失败隔离统一服从 [端到端数据流与生命周期契约](data-flow.md)。

## 1. 对齐规则

每一行必须回答十个问题：

1. 商用级用户能观察到什么行为；
2. 本产品准备采用什么语义；
3. 属于 Core、Pro 还是依赖硬件的选件；
4. 由哪个领域对象或 deep module 负责；
5. Web 与 SCPI 如何访问同一能力；
6. 消费哪个正式数据阶段，能否在 Hold/last-good 上重算；
7. 与哪些 Operation、revision、lease 或状态机相连；
8. 失败、取消、无效点、并发和能力不足时怎样表现；
9. 用什么黄金数据、契约测试或端到端场景验收；
10. 当前究竟是已经明确/由证据定案，还是仍待兼容、算法计量、产品、底软或平台闭合。

“架构中已有位置”不等于“产品行为已经确认”；“Mock 能实现”也不等于“真实单板支持”。

### 1.1 官方证据的边界

商用厂商通常公开用户手册、编程手册、SCPI 命令、自动化对象模型和数据处理行为，但不公开其完整进程、线程、内存和内部模块源码。因此本文严格区分：

| 证据等级 | 含义 | 谁负责作结论 |
|---|---|---|
| E1 跨厂商官方共性 | 至少两家官方资料给出一致的外部对象或行为 | 架构直接采用，用户无需凭专业直觉确认 |
| E2 单厂商/厂商差异 | 官方行为真实存在，但其他厂商不同或未公开 | 给出推荐默认；只有是否兼容某家行为需要用户选择 |
| E3 架构推导 | 为满足已证实的外部行为而设计的内部模块、revision、快照、队列或 seam | 由架构评审和测试负责，不冒充厂商内部实现，也不转嫁给用户 |
| E4 硬件/平台事实 | 只能由公司底软、单板资料、SDK 编译或目标机实测得到 | 由责任方提供证据，未知时保持 TBD |

后续逐组评审先附官方证据和证据等级，再给推荐结论。用户只需要回答真正的产品定位、兼容目标和交互取舍；相关 E1/E2 外部事实的边界、E3 技术设计和 E4 验收责任由设计、代码审查和验收负责。

四组一手证据分别覆盖 [对象、分析与控制行为](../research/official-vna-object-and-analysis-evidence.md)、[Sweep 与采集数据链](../research/official-vna-sweep-acquisition-evidence.md)、[校准与处理链](../research/official-vna-calibration-processing-evidence.md) 以及 [控制、状态、文件、安全与平台](../research/official-vna-control-state-platform-evidence.md)。公开资料只能证明外部行为；公司单板事实和数值算法正确性仍分别由 E4 契约验收与黄金/计量数据关闭。

### 1.2 第一组“对象与所有权”处置结果

证据详见 [商用 VNA 对象、分析与控制行为的一手证据](../research/official-vna-object-and-analysis-evidence.md)。本组不再要求用户判断技术模型是否正确，处置如下：

| 主题 | 证据等级 | 项目结论 | 仍需外部选择 |
|---|---|---|---|
| Channel | E1 | 作为 stimulus、Sweep、Trigger、Average、Correction 的采集主作用域 | 容量和硬件并发属于 E4 |
| Measurement/Trace | E1+E2+E3 | 使用稳定 `AnalysisTrace(TraceSourceSpec)`；Live Source 内含 `MeasurementSpec` 值对象，不建立独立 MeasurementDefinition 聚合 | SCPI 对外叫 Measurement 还是 Trace 由方言决定 |
| Diagram/Window | E1+E3 | `Diagram` 是显示容器，通过 `TracePlacement` 关联 AnalysisTrace，不拥有采集、Marker 或 Limit 判定 | 厂商对 Placement 数量和跨 Channel 布局的限制由 Profile 决定 |
| Marker | E1+E3 | Definition 与 Evaluation 分开，归属于 AnalysisTrace；Diagram 只显示 symbol/readout | 搜索 metric、tie-break 等在 Marker 功能组按目标兼容级别冻结 |
| Limit | E1+E3 | Definition、test enabled、overlay visible、result snapshot 分开，归属于 AnalysisTrace | 厂商快捷命令的组合副作用由 Profile 决定 |
| 完成边界 | E1+E3 | start/accepted 与 completed 分开；正式分析只读完成结果；内部使用不可变 snapshot/revision/pin | `*OPC?/*WAI` 的精确 pending 集合由方言决定 |
| 标识 | E1+E3 | 内部使用带类型稳定 ID，外部数字编号按 documented scope 映射 | 编号复用和最少对象数量由方言决定 |
| 删除 | E2+E3 | 核心拆分 DeletePlacement、DeleteAnalysisTrace、DeleteDiagram、DeleteChannel；历史结果不改写 | 目标方言如何组合这些 Command |
| Web/SCPI selection | E2+E3 | Web selection 固定 session-local；SCPI 提供 SharedInstrument、PerChannelShared、SessionLocal 三种策略实现 | 首个 SCPI 兼容 Profile 采用哪一种 |
| Continuous | E2+E3 | 内部采用 ContinuousRun 父 Operation + 每轮 SweepOperation，完成查询 pin 具体轮次 | 外部同步行为按主方言回归 |

## 2. 状态词汇

| 状态 | 含义 |
|---|---|
| 已明确 | 用户已经明确给出或同意，或者目标环境已有可核实事实 |
| 已由证据定案 | 相关外部事实已按 E1/E2 边界记录（如有），项目原生语义、E3 设计和验收路径已闭合；不代表跨厂商共性或厂商内部实现相同，也不再让用户凭感觉确认 |
| 待产品确认 | 不属于共同最低线，确实需要决定是否交付、产品等级或交互/部署策略 |
| 待兼容目标 | 厂商行为不同，必须在主方言或 Product/Compatibility Profile 选定后冻结 |
| 待算法/计量验证 | 外部能力和架构已确定，但公式、数值稳定性、边界政策或计量精度必须由黄金数据/商用品对照验证 |
| 待底软/硬件确认 | 取决于真实单板拓扑、底软契约、性能或质量标志 |
| 待平台验证 | 取决于 AArch64 SDK、系统库、网络、文件系统或第三方依赖的实测结果 |

## 3. 分册与覆盖范围

| 分册 | 覆盖范围 | 状态 |
|---|---|---|
| [01 Instrument、采集与校准](alignment/01-instrument-acquisition-calibration.md) | Instrument、Channel、Stimulus、Sweep、Trigger、Average、Measurement、资源仲裁、Operation、Calibration | 46 项，已完成证据归类 |
| [02 分析与显示](alignment/02-analysis-display.md) | Trace、Diagram、格式、Marker、Limit、Math/Memory/Hold/Statistics、参考面、夹具、混模、时域 | 66 项，已完成证据归类 |
| [03 控制、文件、诊断与平台](alignment/03-control-files-platform.md) | Web、SCPI、Session、状态寄存器、文件、State、诊断、安全、构建、依赖和容量 | 64 项，已完成证据归类 |

当前共盘点 176 项：6 项“已明确”、67 项“已由证据定案”、20 项“待算法/计量验证”、27 项“待兼容目标”、24 项“待底软/硬件确认”、22 项“待平台验证”，仅 10 项仍是真正的“待产品确认”。这个数量表示评审粒度，不表示功能数量或开发任务数量；同一行可能仍在说明中附带次级门禁，状态列记录其首要未闭合条件。

这次重分类的目的不是把风险标成“完成”，而是把责任放到正确位置：通用对象和行为由架构承担，厂商差异由 Compatibility Profile 承担，数值正确性由黄金/计量验证承担，单板事实由 Real Adapter 契约承担，SDK/容量由目标机验证承担；只有功能是否交付和部署政策才留给产品决定。

### 3.1 仅剩的 10 项产品决策与推荐默认

| ID | 真正需要决定的范围 | 推荐默认 |
|---|---|---|
| LIM-05 | 无效数据如何汇总到生产判定 | 核心保持 `Indeterminate`；生产 Safety Policy 可将其汇总为 Fail，绝不当 Pass |
| LIM-10 | Ripple、mask、连续 N 次等专业 Limit 模板 | 独立 Pro evaluator；普通 Segment Limit 保持 Core |
| MATH-08 | 同一点跨 Sweep 的 ensemble statistics | Pro；与沿 X 的单 Sweep statistics 分型 |
| MATH-10 | 跨 Trace/Channel Equation Editor | Pro；只允许有类型、有界表达式图，不引入任意脚本 |
| NET-03 | Renormalization 的 Core/Pro 深度 | Core 支持逐端口正实数 Z0；complex/balanced Z0 与可选 wave theory 归 Pro |
| NET-10 | AFR、enhanced TDR、eye/mask | 独立 HW/Option 或 Pro，不冒充基础时域/去嵌 |
| FILE-10 | PDF/图片/签名报告 | Pro；所有报告绑定不可变快照和模板 revision |
| SEC-01 | Web 身份来源与角色深度 | Core 本地管理员/操作者/只读、首次改密和超时；企业身份源另列 Pro/部署集成 |
| SEC-03 | SCPI 的网络/认证边界 | 默认仅可信管理网或 allowlist；是否增加认证由部署威胁模型决定 |
| PLAT-12 | Pro/HW 组合与授权 | 静态注册 + ProductProfile gating；不得依赖跨工具链动态 C++ 插件 ABI |

这 10 项不是在问“技术架构是否正确”，而是产品线和部署取舍。其余 166 项已经有明确责任路径；其中 27 项厂商差异可在一次选定首个 SCPI/行为兼容 Profile 后成组冻结。

## 4. 已明确的项目约束

| ID | 已明确事项 | 影响范围 |
|---|---|---|
| K-01 | 目标是公司定制 `AArch64 GNU/Linux 5.10 PREEMPT`，不是裸机 RTOS | 平台抽象、线程、Socket、文件系统、实时边界 |
| K-02 | 生产使用公司 AArch64 Linux SDK；MinGW 用于 Windows Mock、开发和测试 | 双 CMake toolchain、共享 C++ 核心 |
| K-03 | 不实现底软；底软执行逻辑扫描并提供逐频点、逐接收路径的复数 `a/b` 接收机波量与质量信息 | Board Adapter seam、Mock/Real 契约 |
| K-04 | Web 可以显示可丢弃 Preview；正式计算、Marker、Limit、保存和 SCPI 只读取完整快照 | 双数据通道、原子发布、last-good |
| K-05 | 使用 C++17；Eigen3 与 cpp-httplib 只允许作为候选进入准入。cpp-httplib 上游明确未支持/测试 MSYS2（含 MinGW），必须 pin 精确版本并分别通过指定 MinGW-w64 与公司 AArch64 SDK 的门禁，基础 HTTP 不通过即替换整个 Web HTTP Transport Adapter | 核心和 Web Adapter |
| K-06 | 新增目标端 C/C++ 依赖必须先验证 RTOS/AArch64 SDK 编译与运行 | 依赖准入门禁 |
| K-07 | 用户通过网页操作完整网分功能，也可通过 TCP Socket SCPI 控制 | 两个 Transport Adapter、同一 Instrument Kernel；只剩方言差异进入 Compatibility Profile |
| K-08 | 必须预留多种真实单板并提供开发环境 Mock | capability-driven 适配、契约测试、故障注入 |

## 5. 对齐完成门槛

只有满足以下条件，候选架构才能改为冻结产品基线：

- 所有 Core 行均有明确产品语义，不再只有名词或空菜单；
- 每一项 Core 能力都有 Web 与 SCPI 的一致性规则，或明确说明只属于其中一个表现层；
- 每一项正式分析都绑定明确的数据阶段、快照和重算规则；
- 每一项异步能力都有 Operation 终态、取消/超时和失败保持 last-good 的规则；
- 每个真实硬件相关功能都能映射到 BoardCapabilities，未知能力不会由 Mock 冒充；
- 每一项 Core 都有成功、边界、失败和并发验收；
- Pro/HW 项目未实现时通过 capability 明确拒绝，不以占位接口返回成功；
- 所有“待产品确认/待兼容目标”都有证据、推荐值和明确决策；所有“待算法/计量验证”通过规定黄金集；所有阻塞首块真实单板的硬件/平台 TBD 有责任方、输入资料和可重复验证方法。

## 6. 逐组评审顺序

逐项核对按依赖关系分组进行；组内每一行都记录结论，但一次讨论完整功能域，避免孤立概念造成前后矛盾。

1. **仪器对象与所有权**：Instrument、Channel、Measurement、Trace、Diagram、Marker、Limit、Session、稳定 ID/revision 和删除语义。
2. **刺激与 Sweep 执行**：频率/功率/IFBW、Linear/Log/Segment/CW、Single/Continuous/Groups、Trigger、Average 和实际轴。
3. **单板与正式数据链**：Capabilities、资源图、完整逻辑 Sweep、a/b、质量标志、Preview、快照、失败和恢复。
4. **校准闭环**：Cal Kit/Standard、方法、Guided Session、标准件采集、求解、Correction Set、Binding、插值与失效。
5. **Trace 与 Diagram**：格式、坐标、布局、叠加、抽稀、Math、Memory、Hold、Smoothing 和 Statistics。
6. **Marker 与 Limit**：所有 Marker 类型/搜索/耦合/marker-to，以及 Limit Segment/边界/无效点/报告/状态聚合。
7. **专业网络处理**：Port Extension、Z0、Fixture、De-embedding、Mixed-mode、Time Domain、Window 和 Gate。
8. **Web 与并发控制**：完整操作面、事件/Preview、revision、lease、断线和大数据传输。
9. **SCPI 与 IEEE 488.2**：主方言、parser、会话、队列、状态寄存器、完成同步和数据阶段。
10. **State、文件、诊断、安全与平台**：保存恢复、Touchstone/CSV、自检、日志、权限、TLS、依赖和容量。

上述十组的官方证据审查已经完成。后续不再按概念逐个提问，而是按四个闭合包推进：Compatibility Profile、算法/计量黄金集、Board Adapter 契约、AArch64 平台准入；用户只需对本节 10 项产品取舍和首个兼容目标作批量决定，未明确回复时保留推荐默认但不冒充已批准。
