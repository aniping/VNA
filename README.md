# Vector Network Analyzer

面向公司定制 AArch64 GNU/Linux 5.10 PREEMPT 平台的 VNA 上层软件项目。生产程序使用公司 AArch64 Linux SDK 构建；Windows MinGW 用于 Mock 驱动、开发调试和自动化测试。项目不实现单板底软，而通过统一 Board Adapter 接入真实单板、Mock 和回放数据。

## 当前状态

候选分层架构 v0.1、跨层 Interface、Board Adapter、端到端数据流/生命周期契约与 176 项商用功能矩阵已经完成文档基线，其中商用功能已完成官方证据归类；但它们还不是冻结产品规格，也尚未开始业务代码实现。下一阶段通过公司底软说明/HIL、双工具链编译、算法黄金数据和首条 Mock 纵切关闭这些候选契约；不再要求用户凭感觉确认通用技术模型。

## 设计文档

- [分层架构与跨层流动（首读）](docs/design/layered-architecture.md)
- [跨层 Interface 契约基线](docs/design/interface-contracts.md)
- [Board Adapter Interface 与合同测试契约](docs/design/board-adapter-contract.md)
- [整体系统架构](docs/design/system-architecture.md)
- [端到端数据流与生命周期契约](docs/design/data-flow.md)
- [商用功能逐项对齐矩阵](docs/design/feature-alignment-matrix.md)
- [商用 VNA 功能能力目录](docs/research/commercial-vna-capability-catalog.md)
- [商用 VNA 外部行为基线](docs/research/commercial-vna-behavior-baseline.md)
- [商用 VNA 对象、分析与控制行为的一手证据](docs/research/official-vna-object-and-analysis-evidence.md)
- [商用 VNA Sweep 与采集数据链一手证据](docs/research/official-vna-sweep-acquisition-evidence.md)
- [商用 VNA 校准与处理链一手证据](docs/research/official-vna-calibration-processing-evidence.md)
- [商用 VNA 控制、状态、文件、安全与平台一手证据](docs/research/official-vna-control-state-platform-evidence.md)
- [统一业务语言](CONTEXT.md)
- [架构决策记录](docs/adr/)

架构覆盖完整逻辑扫描、校准/修正/独立验证、Trace/Marker/Limit、Diagram、Math/Memory/Hold/Statistics、参考面与夹具、时域与门控、Web/SCPI 同源控制、状态保存、诊断以及多单板能力适配。

## 实现约束

- 核心语言：C++17。
- Windows 开发工具链：MinGW-w64。
- 生产工具链：公司 AArch64 Linux SDK/交叉编译器。
- 用户允许进入准入评估的候选第三方库：Eigen3、cpp-httplib；“允许候选”不等于工具链已支持或可直接用于生产。
- cpp-httplib 上游当前明确不支持也未测试 MSYS2/MinGW；必须 pin 精确版本，在项目指定的精确 MinGW-w64 上先通过 HTTP core 的编译、链接、运行，再验证 SSE/WebSocket，并由公司 AArch64 SDK/目标机独立复验。基础 HTTP 失败时替换整个 Web HTTP Transport Adapter，不能只切换实时传输方式。
- 任何新增目标端 C/C++ 依赖必须分别通过开发工具链与公司 AArch64 SDK 的编译、链接和目标机冒烟验证。
- Preview 仅用于 Web 实时观察；正式计算、Marker、Limit、保存和 SCPI 查询只读取完整、不可变、原子发布的结果快照。
