# 分离校准会话、修正集与 Channel 绑定

> 状态：已由证据定案；求解器仍受算法/计量门禁

Calibration Session 只表示标准件采集和求解过程，成功后发布不可变 Correction Set；Channel 通过独立 `CorrectionBinding = Unbound | Bound{set_id, set_revision, enabled, policy_revision}` 选择 Set 并单独控制 correction on/off，关闭修正不丢失所选 Set。每次实际扫描通过 Correction Match Report 正交评估频率轴、路径、条件、时效与总体适用性，因此换板或改配置不会篡改历史修正集，也不会把“已求解”“已选择”“已启用”和“当前不匹配”压成一个错误的状态机。

Calibration Verification 再与上述三者分离：`VerificationPlanRevision` 固定待验证 Correction Set、独立 verification artifact characterization、端口/轴、tolerance/uncertainty 与算法版本；`CalibrationVerificationOperation` 消费正式 B 层测量并发布不可变 Pass/Fail/Indeterminate 结果。取消、失败或 Pass 都不修改 Correction Set/Binding，system/confidence check 也不得被表述为单个仪器或标准件认证。

校准对象、方法、采集、求解、应用和适用性的官方证据见 [校准与处理链一手证据](../research/official-vna-calibration-processing-evidence.md)。
