# Vector Network Analyzer

面向公司定制 AArch64 GNU/Linux 5.10 PREEMPT 平台的 VNA 上层软件项目。生产程序使用公司 AArch64 Linux SDK 构建；Windows MinGW 用于 Mock 驱动、开发调试和自动化测试。项目不实现单板底软，而通过统一 Board Adapter 接入真实单板、Mock 和回放数据。

## 当前状态

候选分层架构 v0.1、跨层 Interface、Board Adapter、端到端数据流/生命周期契约与 176 项商用功能矩阵已经完成文档基线，其中商用功能已完成官方证据归类。第一条 C++17/MinGW 可执行纵切已经建立：包含 BoardPort 公共类型、`MockBoardProvider`、固定容量 `OperationRuntime`、最小 `InstrumentStore` 生命周期提交和 L2 `SweepAdmissionController`。这些代码用于关闭关键所有权契约，还不是整机产品规格或完整业务实现。

## 当前可执行纵切

已实现：

- `BoardProvider → OpenedBoard.Execution` 的 discover/open/cached capability、prepare 与 run；
- `MockBoardControl` 虚拟时间，以及确定性的 receiver-wave `a/b` chunk 与类型化质量标志；
- prepare/run 同步 Rejected 时归还全部 move-only 输入且零 callback；
- Accepted 后非内联回调、唯一 terminal 和 terminal 后零回调；
- Runtime 固定 slot 的 `reserve_work → dispatch → completion`；
- Store 在 Operation 可见前预留 terminal capacity，初始 commit 失败不派发，已安装 reservation 可在容量压力下提交终态；
- L2 先提交 Accepted Operation、再 dispatch，Runtime completion 再提交 L5 权威终态的跨层合同测试。

当前明确未实现：

- Board Safety/Maintenance、discard/abort/Drain/Replay 和真实底软 Adapter；
- A Builder、B/C 处理链、校准、Trace/Marker/Limit、Diagram、文件与诊断；
- Web、SCPI、cpp-httplib、Eigen3 和 JSON；
- 公司 AArch64 SDK 编译与目标机/HIL 验证。

当前 Mock 单个 contract chunk 使用 64 个复数样本的有界 backing；这是首条合同测试的临时上界，不是产品点数上限。接入正式 BufferPool/Ingress seam 后才能扩大或实现多 chunk，不能把它带入真实板卡能力声明。

## MinGW 构建与测试

要求 MinGW-w64 `g++` 与 Ninja 已加入 `PATH`。Windows 配置会拒绝非 MinGW 编译器。

```powershell
cmake --preset mingw-debug
cmake --build --preset mingw-debug
ctest --preset mingw-debug
```

测试全程使用虚拟时间，不依赖 wall-clock sleep。`BUILD_TESTING=ON` 时，CMake 按固定版本 `v1.17.0` 获取 GoogleTest，并通过 `gtest_discover_tests()` 注册独立测试用例；生产/RTOS 配置设置 `BUILD_TESTING=OFF` 后不会获取或链接 GoogleTest。

## 源码组织

工程采用 Piccolo 风格的运行时主干：根 CMake 只组合 `vna`，`vna/CMakeLists.txt` 再组合三方库、Runtime、Board Adapter 和测试。当前实现位于：

- `vna/source/runtime/core`：基础类型；
- `vna/source/runtime/platform`：平台与 Board seam；
- `vna/source/runtime/resource`：权威事实存储；
- `vna/source/runtime/function`：Operation 与 Instrument 工作流；
- `vna/source/adapter`：Mock 及未来真实单板 Adapter；
- `vna/source/test`：GoogleTest contract/integration 测试。

项目自有头文件统一使用 `.h`，实现文件使用 `.cpp`；文件加入现有 Runtime target 时必须在对应 `CMakeLists.txt` 中显式列出，不使用递归 glob 自动收集源码。

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
