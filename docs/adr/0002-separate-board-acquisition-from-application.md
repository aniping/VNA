# 将单板采集与上层 VNA 逻辑分离

> 状态：已接受

本项目不实现单板底层软件；底软负责硬件操作和逻辑扫频，并通过预留的单板适配接口，向上层交付每个实际激励频点、每条接收路径上已经解调但未经用户校准的复数 a/b 接收机波量，而非 ADC 或 IQ 时域采样。生产环境由各单板的真实适配器实现该接口，开发与自动化测试环境由遵循同一契约的 MOCK 实现；校准、误差修正、测量、显示和远程控制等仪器语义归上层软件所有，以避免板卡差异渗入 VNA 领域逻辑。

候选方法级契约采用显式 `L2 plan/conservative admission → Board prepare → actual Manifest 本地校验与单调收窄（零新分配）→ start`，并把 Execution、Safety、Maintenance 分为三个权限分面；这保留了 Correction match、多板 barrier 与固定容量准入，又不暴露厂商 SDK 微步骤。accepted/terminal、Buffer lease、Mock/Replay 和 Real HIL 门槛见 [Board Adapter Interface 与合同测试契约](../design/board-adapter-contract.md)，共同跨层规则见[跨层 Interface 契约](../design/interface-contracts.md)。详细契约在底软签字前保持候选状态，不改变本 ADR 已接受的责任切分。

每块可发射 RF 的板必须另有不与 Acquisition/Prepare/Recovery worker 共用的 `BoardSafetyLane` 执行 RF-off/readback，并预留可越过该 lane 卡死的物理 interlock/kill；软件 quarantine 不能冒充 RF 已关闭。Board capability 还必须显式描述 Clock/Coherence Domain、timebase lock、同步 trigger/epoch、skew 和实际轴保证；未知时默认一轮 Logical Sweep/校准采集只使用一个 Board Session，不得把跨板数据合成相干网络矩阵。
