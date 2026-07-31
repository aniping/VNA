# 第一阶段：仿真测量闭环

> 目标：在没有真实硬件的条件下，证明统一业务内核、控制面、数据面和多协议入口能够形成一个可重复验证的 VNA 最小闭环。

## 1. 成功标准

用户能够在 Vue 工作台创建并配置一个 Channel，定义 S11 和 S21 Measurement，以多种 Trace 格式显示；命令经统一业务入口生成 SweepPlan，由 Simulation Backend 返回 RawReceiverFrame，经过测量合成和显示处理后，通过 WebSocket 展示并可保存、查询和回放。

该闭环必须证明：

- UI、REST 和最小 SCPI 垂直切片共享相同命令与查询处理器。
- 仪表状态具有单一真值和单调递增的 `stateRevision`。
- Measurement 和 Trace 分离，多 Trace 不产生额外扫频。
- 数据帧携带配置版本，旧配置数据不会伪装成新配置结果。
- 连续扫频、单次扫频、Hold、Abort 和重连具有明确语义。
- 仿真结果可通过固定 Seed 和 Record/Replay 重复。

## 2. 范围

### 2.1 包含

**领域与应用**

- 单 Instrument。
- Channel、Measurement、Trace、Window 的最小完整模型。
- 单通道，模型和接口允许后续扩展多通道。
- S11、S21 Measurement。
- LogMag、Phase、Smith Trace。
- 基础 Marker 读数。
- Start/Stop、Points、IFBW、Power 配置。
- Continuous、Single、Hold、Abort。
- CommandBus、QueryBus、DomainEvent 和 Operation 生命周期。
- StateSnapshot、StatePatch、`stateRevision` 和乐观并发冲突。

**测量与数据**

- Measurement Planner 与 SweepPlan。
- L1 Simulation Backend 的最小实现。
- 可配置的解析 DUT 或内置确定性双端口模型。
- RawReceiverFrame 完整性、序号和质量标志。
- 接收机比值到 S11/S21 复数数据的测量合成。
- Trace 格式转换与 FrameRepository。
- 固定 Seed 噪声。
- RawFrame 和命令日志的 Record/Replay。

**交互与呈现**

- Vue 3 + TypeScript 工作台骨架。
- 使用固定版本的 `yhirose/cpp-httplib` 提供 HTTP/HTTPS 与 WebSocket/WSS。
- REST：快照、配置命令、查询和操作创建。
- WebSocket：状态补丁、事件、进度和二进制测量帧。
- 最小 SCPI 垂直切片，用于验证统一入口：
  - `*IDN?`
  - `SENS1:FREQ:STARt` / `STOP`
  - `SENS1:SWEep:POINts`
  - `CALC1:PARameter:DEFine`
  - `INIT1:IMMediate`
  - `*OPC?`
  - `CALC1:DATA? SDATA`
  - `SYSTem:ERRor?`
- 断线重连后重新同步完整状态。
- 开发诊断页：Command、Operation、SweepPlan、RawFrame 元数据和错误。

**工程质量**

- 基于 CMake 的 Windows/Linux 统一构建入口。
- Windows/MinGW GCC 与 Linux/GCC 的持续集成构建矩阵。
- `cpp-httplib` 在两个平台上的协议适配器集成测试。
- `cpp-httplib` 的 HTTP、WebSocket 兼容性冒烟测试。
- 启用 HTTPS、WSS 或非回环地址监听前完成双平台 TLS 冒烟测试。
- 结构化日志和统一关联标识。
- 核心领域单元测试。
- Backend 契约测试。
- 确定性算法金样测试。
- REST/WebSocket/SCPI 端到端测试。
- 基础性能基线和慢客户端背压测试。

### 2.2 不包含

- 真实硬件、Driver Host 和厂商 SDK。
- 完整 SCPI 命令集、VISA、VXI-11 或 HiSLIP。
- 用户、角色、复杂远程控制权策略和公网部署加固。
- OSM、TOSM、UOSM、TRL 或 ECal。
- CalKit 和正式 CalSet 管理。
- S12、S22、多端口和多通道 UI。
- Segment、Power、CW 或 Time Sweep。
- Averaging、Smoothing、Limit Test、Group Delay。
- 去嵌、混合模、端口延伸和时域门控。
- HDF5、大规模历史数据和正式项目包迁移。
- 产品化外观、国际化和完整无障碍适配。

这些能力不在第一阶段实现，但第一阶段不得做出阻断其后续接入的数据模型或接口捷径。

## 3. 垂直切片顺序

### M1：领域骨架

交付：Instrument、Channel、Measurement、Trace、Window、命令处理和内存状态仓库。

验证：

- Windows 和 Linux 均能 configure、build 并运行领域单元测试。
- 创建 S11 Measurement 和两个不同格式 Trace。
- 删除 Trace 不删除仍被引用的 Measurement。
- 非法频率范围、点数和悬空引用被领域规则拒绝。
- 每次成功状态事务只递增一次 `stateRevision`。

### M1.5：可测试的本地网页壳

交付：仅监听回环地址的最小 `vna-server`、StateSnapshot REST 接口和
Vue 工作台骨架。前端按 [ZNA26 界面复刻基线](ui-zna26-reference.md) 实现，
首版采用官方 Single Window Mode 的 1280×800 主应用屏，必须读取真实服务端
状态，不维护静态业务 Mock。

验证：

- `GET /api/v1/health` 返回服务可用状态。
- `GET /api/v1/state` 返回当前完整 StateSnapshot。
- 页面显示连接状态、`stateRevision` 和核心实体数量。
- 页面发出的最小配置命令经 `POST /api/v1/commands` 进入 CommandBus。
- Windows/MinGW GCC 与 Linux/GCC 均能启动服务并构建前端。

该切片只用于尽早验证交互和进度，使用本地 HTTP。HTTPS、WSS 和远程监听
仍由后续 TLS 兼容性与访问安全切片控制。

### M2：模拟采集

交付：CapabilitySet、Measurement Planner、SweepPlan 和最小 Simulation Backend。

验证：

- S11/S21 合并为一次源端口 1 激励计划。
- 相同输入和 Seed 产生字节级一致的 RawReceiverFrame。
- prepare、acquire、abort 和 health 通过 Backend 契约测试。

### M3：数据处理链

交付：帧校验、测量合成、LogMag/Phase/Smith、Marker 和 FrameRepository。

验证：

- 已知 DUT 模型与理论 S 参数在误差限内一致。
- 同一 Measurement 的多个 Trace 复用复数数据。
- Marker 基于完整分辨率数据，而非显示抽取数据。
- 完成帧及其中间结果不可变。

### M4：Web 控制与实时显示

交付：基于 `cpp-httplib` 的 REST/WebSocket 适配器、Vue 工作台和二进制帧协议。

验证：

- 页面设置频率后收到对应 revision 的状态补丁。
- 扫频帧的 `stateRevision` 与产生它的配置一致。
- 慢客户端在连续扫频中只丢弃过时显示帧，不影响服务端完整帧。
- HTTP/WebSocket Handler 不执行扫频或数据处理，长操作只负责入队并返回 `operationId`。
- 断线重连恢复一致状态，无需页面重建业务真值。
- 启用 HTTPS、WSS 或远程监听前，双平台 TLS 冒烟测试通过。

### M5：操作语义与最小 SCPI

交付：Continuous、Single、Hold、Abort、OperationManager 和最小 SCPI 命令树。

验证：

- `INIT1:IMM;*OPC?` 在完整帧提交后才返回。
- 紧随其后的 `CALC1:DATA? SDATA` 返回同一次已完成扫频数据。
- Abort 后 Backend 资源释放，操作进入确定的终态。
- REST 与 SCPI 对同一非法配置返回语义一致的错误。

### M6：录制、回放与诊断

交付：Command Journal、RawFrame 录制、Replay Backend 和诊断页。

验证：

- 录制内容可在 Replay Backend 中重现相同 Trace 结果。
- 可从 Trace 帧追溯到 RawFrame、SweepPlan、Operation 和 Command。
- 日志和录制文件不包含认证令牌或未声明的敏感数据。

### M7：阶段验收

交付：自动化端到端场景、性能报告、已知限制和下一阶段输入。

验证：所有必需验收场景通过，仓库可在全新开发环境中按文档构建和运行。

## 4. 必需验收场景

| 场景 | 操作 | 预期结果 |
| --- | --- | --- |
| 首次连接 | 浏览器连接服务端 | 收到完整 StateSnapshot 和 CapabilitySet |
| 配置修改 | 客户端基于当前 revision 修改频率 | 命令成功、revision 递增、所有会话收到 StatePatch |
| 并发冲突 | 客户端使用过期 revision 修改点数 | 返回 Conflict，不覆盖新状态 |
| S 参数闭环 | 创建 S11/S21 并触发单次扫频 | 产生一份原始帧并显示两项测量 |
| 多格式复用 | 为 S11 增加 LogMag、Phase、Smith | 不增加激励次数，不重复校准或测量合成 |
| 配置边界 | 连续扫频中修改频率 | 当前帧保留旧 revision，下一帧使用新 revision |
| 完成同步 | 执行 `INIT;*OPC?;CALC:DATA?` | 查询只获得对应的完整已完成数据 |
| 中止 | 采集中发送 Abort | 后端停止、资源释放、操作状态可查询 |
| 慢客户端 | 限制浏览器消费速度 | 只丢过时显示帧，单次扫频和仓库数据不丢失 |
| 断线重连 | 收到若干更新后断开并重连 | 通过差量或完整快照恢复一致状态 |
| 确定性仿真 | 使用相同模型、配置和 Seed 重复运行 | 原始数据和处理结果一致 |
| 回放 | 录制后切换 Replay Backend | 在不运行 Simulation Backend 的情况下重现结果 |
| 双平台构建 | 在 Windows 与 Linux CI 运行工程流水线 | 两个平台均完成 configure、build 和 test |

## 5. 非功能基线

第一阶段开始实施前，为目标硬件规模确定频率点数、扫频速率、并发客户端数和内存上限。具体数值尚未由产品需求确认，不在架构文档中臆测。

必须建立以下可测指标：

- 命令排队与执行延迟。
- 单次扫频准备、采集和处理耗时。
- WebSocket 帧大小与发送延迟。
- Processing Pool 各节点耗时和缓存命中率。
- FrameRepository 容量与淘汰次数。
- 慢客户端丢弃的显示帧数。
- Abort 响应和资源释放耗时。
- 进程内存峰值及每点数据成本。

任何性能优化都必须以这些指标为依据，不提前引入分布式服务、无锁结构或自定义内存池。

## 6. 完成定义

第一阶段只有在以下条件全部满足时才完成：

- 必需验收场景自动化通过。
- Vue、REST、SCPI 均未绕过统一内部契约。
- Simulation Backend 通过 Backend 契约测试。
- 数据帧和状态 revision 能端到端关联。
- 无固定 sleep 参与业务同步。
- 文档包含构建、运行、协议和故障排查说明。
- 全新 Windows 与 Linux 环境都能够按 README 完成构建和测试。
- 性能基线、已知限制和下一阶段风险已记录。

## 7. 第一阶段之后

下一阶段优先补齐两端口测量能力与常用显示分析功能，再进入完整校准和 SCPI 一致性，最后接入真实 Driver Host。接入真实硬件的验收标准是替换 Measurement Backend 后复用同一业务、协议、数据处理和端到端测试，而不是重写系统。
