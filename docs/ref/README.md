# 参考资料

本目录保存产品设计、功能一致性、远程控制和测量算法使用的固定版本参考资料。
参考资料用于解释需求和验证行为，不替代项目自身的领域模型、架构决策和测试合同。
文件名统一使用小写英文 `kebab-case`；手册版本以 `vNN` 后缀标识，来源作者在必要时保留拼音。

## 使用优先级

发生冲突时依次采用：用户当前明确指令与截图、ZNB User Manual v74、用户确认的
ZNB 实机证据、仅作补充的 ZNA User Manual v41、其他技术资料和现有实现。
SCPI 专题优先使用对应远程控制资料，但不能改变已经确认的仪器业务语义。

## 文件说明

| 文件 | 用途 | 约束 |
| --- | --- | --- |
| [ZNB User Manual v74](znb-user-manual-v74.pdf) | ZNB 功能、GUI、菜单路径、默认值和状态转换的主要依据 | UI 与用户可见功能必须优先遵循此版本 |
| [ZNA User Manual v41](zna-user-manual-v41.pdf) | 补充 ZNB 未覆盖的共同测量和显示细节 | 不得用双屏布局或 ZNA 专属行为覆盖 ZNB |
| [Remote Control SCPI Getting Started v04](remote-control-scpi-getting-started-v04.pdf) | SCPI 会话、同步、状态寄存器和 Operation Complete 语义 | 与产品领域真值和正式 SCPI 合同共同使用 |
| [Mixer Measurement Using a Vector Network Analyzer (Lin Mingwei)](mixer-measurement-using-vector-network-analyzer-lin-mingwei.pdf) | 《基于矢量网络分析仪的混频器测量技术研究及实现》的技术背景 | 不是本项目功能或 UI 一致性的优先依据 |

引用具体规范或行为时，应注明资料版本，并尽可能同时注明章节和印刷页码；仅用于声明
资料优先级或建立索引的链接无需页码。原始资料的著作权与商标权归各自权利人所有。
