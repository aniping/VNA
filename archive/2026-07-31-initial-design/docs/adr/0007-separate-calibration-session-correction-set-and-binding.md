# 分离校准会话、修正集与 Channel 绑定

> 状态：已由证据定案；求解器仍受算法/计量门禁

Calibration Session 只表示标准件采集和求解过程，成功后发布不可变 Correction Set；Session、Observation 和 Set 都保留逐板 identity/capability/path condition/evidence 集合，默认单板时长度为 1。Channel 通过独立 `CorrectionBinding = Unbound | Bound{set_id, set_revision, enabled, policy_revision}` 选择 Set 并单独控制 correction on/off，关闭修正不丢失所选 Set。每次实际扫描把完整 `PreparedExecutionManifestSet` 与 Set 比较，Correction Match Report 逐板评估 identity/capability/path/condition 后再聚合频率轴、时效与总体适用性；因此更换任一板或配置不会篡改历史修正集，也不会把“已求解”“已选择”“已启用”和“当前不匹配”压成一个错误的状态机。

每个成功 B 另行冻结 `CorrectionApplication = Unbound | Disabled{set, policy} | Applied{set, match_report}`，表示该快照当时实际使用的修正，而不是以后根据 Channel 当前 Binding 重新解释。Binding 已启用但 Set 不匹配或修正计算失败时，本轮保留 A、失败 Operation 和原 last-good B，不发布带虚构 MatchReport 或空 Correction Set 的 B。本次普通 A→B 纵切只实现 Unbound 分支。

Calibration Verification 再与上述三者分离：`VerificationPlanRevision` 固定待验证 Correction Set、独立 verification artifact characterization、端口/轴、tolerance/uncertainty 与算法版本；`CalibrationVerificationOperation` 消费正式 B 层测量并发布不可变 Pass/Fail/Indeterminate 结果。取消、失败或 Pass 都不修改 Correction Set/Binding，system/confidence check 也不得被表述为单个仪器或标准件认证。

校准对象、方法、采集、求解、应用和适用性的官方证据见 [校准与处理链一手证据](../research/official-vna-calibration-processing-evidence.md)。
