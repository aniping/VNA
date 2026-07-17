# Vector Network Analyzer

面向公司定制 AArch64 GNU/Linux 5.10 PREEMPT 平台的 VNA 上层软件项目。生产程序使用公司 AArch64 Linux SDK 构建；Windows MinGW 用于 Mock 驱动、开发调试和自动化测试。项目不实现单板底软，而通过统一 Board Adapter 接入真实单板、Mock 和回放数据。

## 当前状态

整体架构基线 v0.1 已于 2026-07-17 确认，目前尚未开始业务代码实现。真实单板的端口/接收机拓扑、容量、误差模型、SDK 能力和计量黄金数据将在对应 Profile 接入时补齐。

## 设计文档

- [整体系统架构](docs/design/system-architecture.md)
- [商用 VNA 功能能力目录](docs/research/commercial-vna-capability-catalog.md)
- [商用 VNA 外部行为基线](docs/research/commercial-vna-behavior-baseline.md)
- [统一业务语言](CONTEXT.md)
- [架构决策记录](docs/adr/)

架构覆盖完整逻辑扫描、校准与修正、Trace/Marker/Limit、Diagram、Math/Memory/Hold/Statistics、参考面与夹具、时域与门控、Web/SCPI 同源控制、状态保存、诊断以及多单板能力适配。

## 实现约束

- 核心语言：C++17。
- Windows 开发工具链：MinGW-w64。
- 生产工具链：公司 AArch64 Linux SDK/交叉编译器。
- 已允许的第三方库：Eigen3、cpp-httplib。
- 任何新增目标端 C/C++ 依赖必须先通过 MinGW 与公司 AArch64 SDK 的编译、链接和目标机冒烟验证。
- Preview 仅用于 Web 实时观察；正式计算、Marker、Limit、保存和 SCPI 查询只读取完整、不可变、原子发布的结果快照。
