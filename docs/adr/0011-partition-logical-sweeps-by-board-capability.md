# 按单板能力划分 Board Run

> 状态：已接受

一个用户可见 `LogicalSweep` 可以由一个或多个实际 `BoardRun` 组成，Sweep Compiler 根据冻结测量需求、路由约束和 Board Capabilities，把所需 source states 与 receiver observations 分成最少但完整的 Board Run 分组；每个 Run 可覆盖一个或多个 source state，而不是全产品固定为“每源一个 Run”或“全矩阵一个 Run”。每个分组拥有自己的 `PreparedExecutionManifest`、accepted/terminal 和 `BoardRunEvidence`，同一板卡可以在一个 Logical Sweep 中产生多项 evidence；所有必需 Run 的容量在首次 RF start 前预留，任一 Run 失败都使整个 Logical Sweep 失败且不发布部分 A/B。Mock 的执行时间由具体 Run Profile 声明，现有 350±50ms 只约束单源 Run，不能外推为多源 Run 的固定总时长。
