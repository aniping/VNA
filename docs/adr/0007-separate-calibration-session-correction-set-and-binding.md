# 分离校准会话、修正集与 Channel 绑定

Calibration Session 只表示标准件采集和求解过程，成功后发布不可变 Correction Set；Channel 通过独立 Correction Binding 选择是否使用某个修正集。每次实际扫描通过 Correction Match Report 正交评估频率轴、路径、条件、时效与总体适用性，因此换板或改配置不会篡改历史修正集，也不会把“已求解”“正在应用”和“当前不匹配”压成一个错误的状态机。
