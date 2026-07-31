# 核心软件原生支持 Windows 与 Linux

`vna-server`、VNA Core、算法、仿真、协议和 CLI 必须在 Windows 与 Linux 上原生构建和运行，操作系统差异限制在平台适配器和具体驱动实现中；Driver Host 可以因厂商 SDK 使用不同平台实现，但必须遵守同一版本化 IPC 与 Measurement Backend 契约。该选择增加双工具链 CI 和平台适配成本，换取部署自由、可移植测试环境，并避免业务核心被 Win32 或 POSIX 细节锁定。
