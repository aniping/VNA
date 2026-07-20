# Web 与 SCPI 不暴露内部 revision

> 状态：已由产品决策定案

Web 和 SCPI 的请求、响应与事件都不携带内部对象 revision，也不接受 `expected_revision`。唯一 Control Executor 按服务器接受顺序处理命令；处理 `StartSweep`/`INIT` 时，Instrument Kernel 从同一授权 Catalog cut 解析当前 Channel 及其内部配置版本、Profile、Capability 和 Topology，并把这些事实冻结进本轮 Operation。协议 Adapter 不得先读取 revision 再代客户端补入。

内部 revision 仍用于不可变结果来源、异步计划重验、`DomainCommitBundle` 前置条件、Head CAS、stale 判定、审计和故障诊断，只是不序列化到 Web/SCPI。Web 增量同步使用不可比较、不可用于 mutation 的不透明 `WatchResumeToken`；SCPI 保持连接内因果顺序。字段/行级 patch 只修改其声明范围，显式整表替换才替换整个对象；复合 patch 仍须整体校验并原子提交，长操作仍用权限与 lease 防止非法并发。当前 Sweep 始终使用接受时冻结的配置，之后的修改只影响后续 Sweep。普通 Channel 同字段竞争究竟采用“后接受者生效”还是显式编辑 lease，是不暴露 revision 之后仍需单独确认的产品政策。
