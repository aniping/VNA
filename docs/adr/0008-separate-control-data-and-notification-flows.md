# 分离控制流、正式数据流与通知流

> 状态：已由架构定案；Buffer ABI、容量与目标平台仍受 E4/平台门禁

VNA 同时存在三条正交流：Command/revision/Operation 构成控制流，A/B/Stage/C 与校准观测构成正式数据流，Catalog commit/EventCursor 构成通知流。三者可以引用相同的 typed ID，但不得互相替代：Operation 只拥有工作、预算和执行 lease；Event 只携带有界 metadata/软引用，可以显式 gap 且不保活数据；QueryTicket 只拥有某个调用者的 waiter、ResultPin/Reader 能力。

正式数据统一由 Measurement Data Store/SnapshotCatalog 管理不可变 Buffer、typed parent closure、QualityPlane、retention 与 tombstone。只有 Control Executor 能取得 `DomainCommitPermit`；Board/Processing/Calibration worker 只能消费冻结输入并返回持有 `CandidateCommitLease` 的 `PublicationCandidateBatch`，不能直接写 Catalog、更新 Head 或发布 Event。Persistence worker 仍按其 deep Module API 返回 Load/Commit/Export 结果，任何会改变在线 Catalog 的 load/recall/import 结果都必须先转成经 Control Executor 验证的领域 candidate，不能由 Persistence 直接发布。异步计算派发前原子取得全部父输入的 `PinnedInputSet`，不可中断任务转 Drain 时连同输入、输出 reservation 和预算一起转移；worker return 后候选租约持续到 commit/abort，不留下所有权缺口。

Control Executor 把可选候选批、有类型的 `DomainCatalogPatchSet`、Head、Operation/fence、Instrument Status Register、SCPI Session State、WaitRegistry、QueryTicket/ResultPin、EventJournal 和 retention patches 组装为一个 `DomainCommitBundle`，交给 `DomainCommitCoordinator` 全有或全无提交。Domain Catalog patch 覆盖 Instrument/Channel/Calibration/Analysis/Display 等小型可变 revision，不得变成无 schema 的 key/value。B、对应 accumulator snapshot、`ChannelAverageHead`、`ChannelMeasurementHead` 与完成状态必须同批可见；CorrectionSet publication 与 CalibrationSession terminal、Recall 的 Instrument revision 切换也必须同批；只有匹配 current-input token 的 Live C 才把 C closure、`TraceAnalysisHead` 与发布事件同批提交，历史 B/Stage exact query 默认只发布/返回 C 而不倒退 Head。只读取既有结果时可以没有候选批，但 direct Ready admission 或 Query Pending→Ready 都必须与覆盖 publication、children 和结构共享 Buffer 的 `ResultClosure` pin 同批取得。`open_read` 把 Ready→Reading 与 ResultPin→ReaderLease 原子转换，传输 terminal 再把 Reading→Consumed/Failed 与 ReaderLease 释放同批提交。任何提交失败都不得留下半套 Snapshot、半更新领域 revision、已推进 Head、状态/唤醒缺口、Ready ticket 或孤立 Event。

采集侧同样遵循唯一所有权：Board Adapter 把 move-only `AcquisitionChunkLease` 交给有界 Acquisition Ingress，Network Observation Builder 是唯一长期拥有者。底软 buffer 不能转移时在回调边界复制一次到预留 BufferPool；Preview 只能读取有界 `ChunkReadView` 或拥有独立、可丢弃的 `PreviewTile`。正式 chunk 不能静默丢弃，Preview 拥塞也不能反压采集。

该决策使 Web/SCPI/File 只在线缆编码和会话语义上不同，无法各自维护“当前数组”；也使事件丢失、慢客户端、retention、取消与 Drain 不会破坏正式事实或完成 fence。代价是必须实现强类型 ID、Data Store/Domain Commit permit、Head、lease 配额、候选批和全有或全无领域提交及相应竞态测试。完整规则与验收场景见 [端到端数据流与生命周期契约](../design/data-flow.md)。
