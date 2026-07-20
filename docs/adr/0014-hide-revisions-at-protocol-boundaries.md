# Web 与 SCPI 不暴露内部 revision

> 状态：已由产品决策定案

Web 和 SCPI 的请求、响应与事件都不携带内部对象 revision，也不接受 `expected_revision`。唯一 Control Executor 按服务器接受顺序处理命令；处理 `StartSweep`/`INIT` 时，Instrument Kernel 从同一授权 Catalog cut 解析当前 Channel 及其内部配置版本、Profile、Capability 和 Topology，并把这些事实冻结进本轮 Operation。协议 Adapter 不得先读取 revision 再代客户端补入。

内部 revision 仍用于不可变结果来源、异步计划重验、`DomainCommitBundle` 前置条件、Head CAS、stale 判定、审计和故障诊断，只是不序列化到 Web/SCPI。Web 增量同步使用不可比较、不可用于 mutation 的不透明 `WatchResumeToken`；SCPI 保持连接内因果顺序。普通配置使用字段/稳定行 ID 的窄 patch，显式整表替换才替换整个对象；复合 patch 仍须整体校验并原子提交。不同字段的合法 patch 各自保留，同一字段以后被 Control Executor 接受且成功提交的修改为准；普通配置编辑不取得 edit lease。校准、Recall 等需要跨多个控制回合保持 owner 的长 Operation 仍用权限与 lease 防止非法并发。当前 Sweep 始终使用接受时冻结的配置，之后的修改只影响后续 Sweep。
