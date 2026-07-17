# 采用双目标、单核心的工具链策略

> 状态：已接受

目标设备运行公司内部定制的 AArch64 GNU/Linux 5.10 PREEMPT 系统，标准 MinGW 不能生成其生产程序所需的 Linux ELF。生产版因此使用公司 SDK 提供的 AArch64 Linux 交叉工具链，Windows MOCK、开发调试与自动化测试使用 MinGW；两端共享纯 C++17 业务核心，并通过平台接口隔离操作系统能力，以兼顾目标兼容性和本地可测试性。
