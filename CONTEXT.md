# 矢量网络分析

本上下文描述 VNA 上层软件中与激励、接收、测量、校准和结果呈现有关的统一业务语言。

## 采集

**接收机波量（Receiver Wave Quantity）**：
在一个实际激励频点和一条接收路径上，由底软完成解调后产生的、未经用户校准的复数入射波 `a` 或响应波 `b`。它不是 ADC 或 IQ 时域采样序列。
_避免使用_：a/b 原始波形、ADC 波形

**扫频预览（Sweep Preview）**：
当前扫频已经采集但尚未形成完整点集的临时数据。它只用于实时观察，不是可供正式计算或查询的测量结果。
_避免使用_：实时结果、当前结果

**完整扫频快照（Completed Sweep Snapshot）**：
在同一采集配置下成功完成一次逻辑扫频后得到的完整点集。它是校准、测量、分析、保存和远程查询所使用的正式输入。
_避免使用_：当前数组、活动数据

**逻辑扫频（Logical Sweep）**：
为产生一个完整测量结果而必须原子完成的全部采集动作。完整双端口 S 参数测量通常包含正向、反向及板卡误差模型要求的辅助观测；任何必要动作失败都不发布部分网络结果。

**单板适配器（Board Adapter）**：
隔离公司底软或 Mock 实现的唯一硬件 seam。它负责报告能力、准备和执行逻辑扫描、上送接收机观测及健康信息，但不实现用户校准、Marker、Limit、Diagram 或协议业务。

**单板能力描述（Board Capabilities）**：
单板可执行频率、功率、IFBW、点数、端口路由、触发、接收机拓扑、波量定义、质量标志和并发资源关系的版本化事实。上层根据它验证和编译扫描，不根据板卡型号散布条件分支。

## 测量与校准

**测量定义（Measurement Definition）**：
描述“测什么”的稳定定义，例如 `S11`、`S21`、接收机波量或接收机比值。它不包含颜色、坐标轴或窗口位置。

**测量完成快照（Completed Measurement Snapshot）**：
由完整逻辑扫频经过接收机量提取、兼容平均和校准修正后原子发布的不可变网络结果，并绑定实际激励轴、配置版本、单板身份和质量信息。

**校准会话（Calibration Session）**：
组织标准件采集和求解过程的有终态操作。成功完成时发布不可变 Correction Set；它本身不表示某个 Channel 正在应用校准。

**修正集（Correction Set）**：
由一次成功校准求解产生的不可变误差模型版本，包含误差项、适用范围、求解器版本和采集来源。

**校准绑定（Correction Binding）**：
Channel 对某个 Correction Set 的显式引用，状态为关闭或绑定。一个 Correction Set 可以同时被多个 Channel 使用。

**校准匹配报告（Correction Match Report）**：
把 Correction Set 与本次实际 Sweep Manifest 比较后的结构化结论，分别表达频率轴、路径、条件、时效、绑定和总体适用性，不压缩成单一“有效/无效”布尔值。

**类型化处理图（Typed Processing Graph）**：
连接校准后网络、参考面移动、参考阻抗转换、夹具、混模、时域、门控、Trace Math 和格式化等节点的版本化图。每个节点声明输入输出数据阶段、轴、端口拓扑、参考面、Z0、有效性传播和 Preview 能力。

## 分析与显示

**迹线定义（Trace Definition）**：
描述一条可分析迹线的稳定身份，引用 Measurement、处理图和分析投影，并拥有 Marker、Limit、Memory、Hold 与统计定义。它独立于任何 Diagram，可在不重新扫频的情况下基于 last-good 数据重新求值。

**迹线求值快照（Trace Evaluation Snapshot）**：
某个测量快照与某个 Trace Definition 版本计算得到的不可变结果。Marker、Limit、SCPI 查询和导出必须绑定明确的求值快照。

**标记定义（Marker Definition）**：
属于 Trace Definition 的分析规则，包括普通、参考、差值、固定、跟踪以及峰值、目标、带宽等搜索。标记计算结果与定义分开，并记录输入节点、数据投影和父快照。

**限制测试定义（Limit Test Definition）**：
属于 Trace Definition 的分段标量判定规则，描述上限、下限、断开段、插值和无效点政策。Limit Line 是同一定义的显示 Overlay，不能另用一套判定算法。

**迹线呈现（Trace Presentation）**：
Diagram 对 Trace Definition 的一个显示引用，只包含可见性、颜色、线型、坐标轴分配、缩放和层级等视图属性。

**图表（Diagram）**：
实际绘图区，拥有坐标系、轴、网格、标题、Trace Presentation 顺序及 Marker/Limit Overlay。它不拥有 Measurement、Trace Definition 或正式分析结果；删除 Diagram 不删除这些对象。

## 控制与执行

**操作（Operation）**：
扫频、校准、迹线重算、保存恢复和导出等异步工作的统一可等待生命周期。Web 事件和 SCPI `*OPC?` 等同步机制等待具体 Operation 的终态，而不是读取全局忙碌布尔值。

**客户端会话（Client Session）**：
每个 Web 或 SCPI 连接独立的选择上下文、权限、错误队列和协议状态。选择 Channel、Trace 或 Marker 不会悄悄改变其他连接的选择上下文。
