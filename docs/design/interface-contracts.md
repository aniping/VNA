# VNA 跨层 Interface 契约基线

> 状态：候选契约 v0.1。本文把[六层职责模型](layered-architecture.md)落实到 C++17 Interface、所有权和终态规则；它不是头文件实现，也不冻结尚未取得的底软或目标 SDK 事实。单板 seam 的字段级细节见 [Board Adapter 契约](board-adapter-contract.md)。

## 1. 本文解决什么

分层图回答“责任属于哪里”，本文进一步回答：

- 相邻层之间允许传递哪些类型；
- 一次调用何时只是 accepted，何时才是真实 terminal；
- Buffer、输入 pin、输出 reservation 和 candidate 由谁拥有；
- cancel、deadline、Drain、断线或提交失败后谁继续保活资源；
- 哪个 Interface 可以改变权威事实，哪个只能返回候选；
- Real/Mock/Replay、Web/SCPI 和行为测试如何穿过同一 seam。

本文不定义：

- HTTP 路由和具体 SCPI 命令名；
- 厂商 SDK、寄存器、DMA、ADC/IQ 或线程句柄；
- Eigen 矩阵类型、JSON DOM、`httplib` 类型或文件描述符；
- 尚未通过黄金数据验证的算法公式；
- 目录中每个类和私有辅助函数。

逻辑契约与物理代码布局必须区分：一个 Interface 可以由多个源文件实现；不得为了让目录看起来“分层”而增加只转发参数的浅 Module。

## 2. 全部公开 Interface 共同遵守的规则

### 2.1 只使用项目自有类型

公开 Interface 只出现以下几类项目类型：

| 类型类别 | 用途 | 规则 |
|---|---|---|
| typed ID | `OperationId`、`BoardRunId`、Snapshot ID 等 | 不同 ID 禁止隐式转换；值不能被调用者猜测为数组下标 |
| revision/epoch | Catalog revision、Profile revision、capability revision、topology epoch | 每次乐观写入或资源授权都携带预期值 |
| 有界值对象 | Command、Intent、Manifest、Quality、Error | 容量在 ProductProfile 或 Capability 中有硬上限；不得使用任意字符串字典扩展语义 |
| 只读 View | Axis、complex values、quality plane | View 的有效期必须被同一个 handle/lease 明确覆盖 |
| move-only Lease | input pin、Buffer、reservation、candidate、reader | 不可复制；移动后原持有者不得访问；析构只做本地资源归还，不承担硬件安全动作 |
| typed Result/Terminal | accepted/rejected、success/failure、terminal phase | 不以 `bool + 日志字符串` 表达错误；accepted 永远不等于 terminal |

禁止把 `std::filesystem::path`、原生 socket、OS handle、厂商枚举、Eigen、JSON 或 `httplib` 类型穿过 Module Interface。C++17 没有 `std::span`，项目应提供轻量只读 `ArrayView<T>`；它只描述视图，不拥有数据。

### 2.2 跨 seam 不传播异常

逻辑 Interface 采用 `Result<T, E>` 或有类型 terminal，所有跨 seam 方法均视为 `noexcept` 契约：

- Adapter/Module Implementation 可以在内部使用受控异常，但必须在 seam 内转换；
- `std::bad_alloc`、未知异常或 SDK 异常不能越过 seam；
- 意外异常转换为 `InternalFailure`，同时触发当前 Operation 的唯一失败路径；
- 任何错误对象都包含稳定 code、phase、关联 typed ID、retry classification 和 safety impact；诊断文本仅作有界附注，调用者不得解析文本做业务判断。

### 2.3 accepted、visible 和 terminal 是三件事

| 时刻 | 含义 | 可以做什么 | 不能做什么 |
|---|---|---|---|
| `Accepted` | 有界队列/Adapter 已接管请求及其资源 | 返回 Operation/Call ID；允许后续定向 cancel/abort | 声称工作完成、释放输入、发布结果 |
| candidate returned | L4 已产生不可见结果并交还真实 worker terminal | L2 可以校验并组装 `DomainCommitBundle` | Web/SCPI 查询、推进 Head、发送完成 Event |
| commit visible | L5 全有或全无提交成功 | Catalog、Head、Ticket、Status、Event 同代可见 | 修改已发布 Snapshot |
| resource terminal | worker、SDK job、回调和硬件资源均已终止或所有权已转入显式 Drain/Quarantine | 释放或转交 lane、lease、reservation | 用“用户已看到超时”冒充资源已经停止 |

每个异步 Interface 必须规定：Rejected 时是否产生 terminal、Accepted 后由哪个 Sink 产生恰好一次 terminal、terminal 之前允许哪些事件、terminal 后是否允许任何回调。本项目默认规则是：**Rejected 不产生回调；Accepted 的首个 callback 必须晚于 submission 返回，且必须产生恰好一次匹配 ID/generation 的 terminal；terminal 后零回调。** 这避免调用者在尚未保存 accepted token 时被 inline completion 重入。

### 2.4 所有容量都在执行前取得

不得在开始接收正式数据后才“尽量申请”关键资源。派发前按工作类型原子取得：

- queue/lane slot；
- deadline 与预算；
- 全部父快照的 `PinnedInputSet`；
- 输出 `OutputReservation`；
- Board prepare/start 授权及采集 Buffer 上界；
- candidate-to-commit、Query Ready/Reading 和临时文件配额。

若无法完整取得，入口返回 `ResourceExhausted` 或 Operation 失败，但不得留下半套 pin、半个 Ticket 或已启动硬件。

### 2.5 析构不是控制命令

RAII 只保证进程内所有权归还。以下动作必须有显式、可观察的工作流，不能藏在析构函数里：

- abort 正在执行的 Board run；
- RF-off、safe-state readback 或 emergency kill；
- 等待不可中断 SDK/文件/算法调用；
- 把 Online Board 清除 quarantine 后重新加入 ResourceGraph；
- 把 QueryTicket、Operation 或 Calibration Session 标记完成。

析构函数不得无限阻塞。持有未终止外部义务的对象只能把义务显式转给 Runtime 的 Drain/Quarantine owner，不能静默放弃。

### 2.6 回调不得反向进入领域核心

L3/L4/L6 的 callback 只允许：

1. 验证 ID、generation 和有界元数据；
2. 移动 lease 到已预留的 ingress；
3. 更新该工作私有的 ledger；
4. 投递有界 progress/terminal。

不得在 SDK、worker 或 Watch callback 中同步反调 `InstrumentKernel`，不得持 Store/Catalog 锁调用外部 Sink，也不得在回调线程执行网络、JSON、文件或 Eigen 重计算。

## 3. 跨层对象词汇

下面的类型是逻辑契约；字段应在后续 C++ header 设计中保持有类型、有界和可序列化。

```cpp
template<class Value, class Error>
class Result;

template<class T>
class ArrayView;                  // non-owning, read-only

template<class Tag>
class TypedId;

struct ExecutionContext {
    StopToken stop;
    MonotonicDeadline deadline;
    BudgetHandle budget;
    ProgressSink& progress;
};

struct RequestContext {
    RequestId request_id;
    AuthenticatedActorRef actor;
    SessionId session;
    CompatibilityProfileRevision profile;
    MonotonicDeadline deadline;
    SessionSequence sequence;
    Optional<CausalPredecessor> predecessor;
};

enum class ReadTerminal {
    Consumed,  // codec 正常完成全部响应
    Failed,    // codec 或 Store 读取失败
    Abandoned  // client/transport detach、断线或超时
};
```

`RequestContext` 是 transport-auth、session、Profile、deadline 和因果顺序的唯一来源；`CommandEnvelope`/`QueryEnvelope` 只含有界 typed payload、target/expected revision 与可选 idempotency，不得再内嵌另一份 context。Kernel 重新验证该上下文并把所需字段冻结进工作或 Ticket。

| 对象 | 产生者 | 消费者 | 所有权/可见性 |
|---|---|---|---|
| `CommandEnvelope` | L1 | L2 | move-only bounded value；不含协议 DOM/字符串树 |
| `QueryEnvelope` | L1 | L2 | 接受时冻结 target、stage、axis、Profile 语义 |
| `FrozenWorkItem` | L2 | L3 | 不再读取 current selection；含明确工作 variant 和 immutable refs |
| `WorkPermitSet` | L2 组装；成员能力由 L3 `reserve_work`、L5 pin/reserve、ResourceArbiter 等分别签发 | L3 | lane/budget/input/output/board authorization 的 move-only 聚合；L5 不创建、接收或看见该 Runtime aggregate |
| `PinnedInputSet` | L5 | L4 或 Drain | 全部 typed parents 原子 pin；只整体成功或失败 |
| `OutputReservation` | L5 `InstrumentStore` | L4 或 Drain | 覆盖最大输出和临时工作区；直到真实 terminal/candidate 交接；Runtime/预算 Module 只签发不同类型的 work/budget capability |
| `LifecycleTerminalReservationSet` | L5 预留，初始 Accepted/Pending commit 后留在 L5 | L5 terminal/Drain handoff commit | 每个可见 Operation/Ticket 及该 work 声明的至多一个 contingency child Drain 在创建前取得；保证终态可落盘，不进入 Runtime/Worker |
| `PendingResultPinReservation` | L5 按 caller/target/保守 closure 上界预留，Pending commit 后留在 Ticket | L5 Pending→Ready commit 或 Ticket 终止 | single-flight 每个 waiter 独立计费；转换为精确 `ResultPinLease`，取消/TTL 释放；一个 waiter 不回滚共享 publication |
| `PublicationCandidateBatch` | L4 | L3 → L2 → L5 | commit 前不可见；内含 `CandidateCommitLease` |
| `DomainCommitBundle` | L2 | L5 | 唯一事实发布载体；commit 在成功或失败时都消费 candidate ownership |
| `ResultPinLease` | L5 commit | L5 Store 内的 Ready Ticket | 完整结果闭包的保活能力；不离开 Store，也不由 L2 暂存 |
| `QueryReadHandle` | L5 Store | L2 → L1 codec | 一次性读取授权；内部拥有由 ResultPin 原子转换而来的 `ReaderLease`，不暴露裸 Buffer |
| `BlobWriteHandle` | L2 upload admission | L1 Binary Transfer lane | actor/session/intent/quota/TTL 绑定的 staging capability；大字节不进入 CommandEnvelope |
| `AuthorizedPreviewPublisher` | L2/L3 admission | L4 或 Preview Drain | 只允许某 Operation/generation 向有界 PreviewHub 投递 provisional tile；不授予正式发布权 |
| `EventRecord` | L2 bundle/L5 Journal | L5 EventFeed → L2 Watch projection → L1 dispatcher | 仅 typed ID、revision、cursor 和有界摘要；不保活数据 |

## 4. Interface 地图

| 调用位置 | 深 Module Interface | 主要输入 | 唯一输出/终态 | 明确禁止 |
|---|---|---|---|---|
| L1 → L2 | `InstrumentKernel` | Command、Query、授权上下文、View/Watch/Preview/Upload 请求 | Submit、Ticket、Reader、View、Event/Preview/Upload terminal | 协议层读 Catalog/Buffer 或实现领域规则 |
| L2 → L3 | `OperationRuntime` | WorkAdmissionClaim；随后 FrozenWorkItem + 全部 permits | `ReservedWorkDispatch{WorkId, WorkDispatchPermit, RuntimeCompletionRegistration}`；typed runtime completion 或显式 Drain ownership | Runtime 解释 Channel/SCPI 或 commit |
| L2 内部 planning | `SweepAdmissionPlanner` | 冻结的 Channel/Profile/Capability/Topology planning input | 无副作用的 `SweepAdmissionPlan` 或 typed rejection | Planner 分配资源、调用 Board 或创建 Operation |
| L3 → L4 Acquisition | `AcquisitionEngine` | FrozenSweepJob + acquisition leases | `Succeeded(A candidate) / Failed / Draining` | worker 更新 Head/Event |
| L3 → L4 Measurement | `MeasurementPipeline` | FrozenProcessingJob + pinned inputs/output | `Succeeded(B/Stage/C candidates) / Failed / Draining` | Eigen 类型出 seam |
| L3 → L4 Calibration | `CalibrationModule` | FrozenCalibrationJob + observations/output | `Succeeded(Observation/Correction/Verification candidate) / Failed / Draining` | Session 流程藏进 solver |
| L3 → L4 Persistence/Diagnostics | 各自 deep Interface | frozen request + authorized inputs | `Succeeded(typed result) / Failed / Draining` | worker 直接改变在线 Catalog |
| L2 → L5 | `InstrumentStore` | typed refs、permits、DomainCommitBundle | Catalog cut、pin/reservation/reader/commit receipt | L4/L1 绕过 L2 发布事实 |
| L4 Acquisition → L6 | `BoardPort` seam | Intent、授权、run/safety 请求 | Manifest、chunk/phase/terminal、安全证据 | 单板 Adapter 组合多板或理解 VNA 领域对象 |
| L4 → PreviewHub → L1 | `AuthorizedPreviewPublisher` / Kernel Preview subscription | bounded provisional tile；授权 registration | 可丢 tile、显式 gap、stream terminal | L4 保存 Web callback、Preview 进入 L5 或跨 actor 泄露 |

依赖箭头不要求每次调用机械经过六层。例如配置 Command 从 L2 直接提交 L5；已有结果的 Query 从 L2 直接从 L5 取得闭包。禁止的是责任越权，不是合法的短路径。

## 5. L1 → L2：InstrumentKernel Interface

```cpp
class InstrumentKernel {
public:
    virtual SubmitResult submit(CommandEnvelope&& command,
                                const RequestContext& request) noexcept = 0;

    virtual QueryAdmission admit(QueryEnvelope&& query,
                                 const RequestContext& request) noexcept = 0;

    virtual Result<QueryTicketView, QueryError>
    inspect(QueryTicketId ticket,
            const QueryAccessContext& access) const noexcept = 0;

    virtual Result<QueryReadHandle, QueryError>
    open_read(QueryTicketId ticket,
              const QueryAccessContext& access) noexcept = 0;

    virtual FinishReadResult finish_read(QueryReadHandle&& handle,
                                         ReadTerminal terminal) noexcept = 0;

    virtual CancelQueryResult cancel_query(QueryTicketId ticket,
                                           const QueryAccessContext& access) noexcept = 0;

    virtual Result<InitialViewSnapshot, ViewError>
    initial_view(const InitialViewRequest& request) const noexcept = 0;

    virtual WatchSubmission begin_watch(
        const WatchRequest& request,
        WatchSinkRegistration&& sink) noexcept = 0;

    virtual StopWatchResult stop_watch(
        WatchId watch,
        const WatchAccessContext& access) noexcept = 0;

    virtual PreviewSubmission begin_preview(
        const PreviewRequest& request,
        const PreviewAccessContext& access,
        PreviewSinkRegistration&& sink) noexcept = 0;

    virtual StopPreviewResult stop_preview(
        PreviewSubscriptionId subscription,
        const PreviewAccessContext& access) noexcept = 0;

    virtual BlobWriteAdmission begin_blob_write(
        UploadIntent&& intent,
        const RequestContext& request,
        BlobWriteCompletionRegistration&& completion) noexcept = 0;

    virtual BlobChunkWriteResult write_blob_chunk(
        BlobWriteHandle& handle,
        BlobChunkLease&& chunk) noexcept = 0;

    virtual BlobWriteFinishResult finish_blob_write(
        BlobWriteHandle&& handle,
        BlobWriteTerminal terminal) noexcept = 0;
};
```

### 5.1 submit

- L1 已把 HTTP/SCPI 语法转换成有类型 Command，但 L2 重新验证 actor、session、Profile、capability、revision 和领域不变量。
- 纯配置 Command 只在 `DomainCommitBundle` 成功后返回 committed revision。
- 长操作返回 `Accepted{OperationId, accepted_revision}`；这不是完成。
- admission 失败不创建幽灵 Operation；若创建 Operation 与派发之间失败，Operation 必须有可查询的失败终态。
- 同一 SCPI Session 的 `session_sequence + causal_predecessor` 保持 Profile 规定的因果顺序；Web 的 expected revision 冲突明确返回。

### 5.2 admit/inspect/open_read/finish_read

- `admit` 在接受时冻结 actor、Profile、typed target、data stage、axis、具体 completed parent 和 TTL。
- 已物化结果先预留该 Ticket 的 lifecycle terminal capacity，再通过一次 state-only commit 同时安装 reservation、创建 Ready Ticket 和精确 `ResultPinLease`；失败不创建 Ticket。
- 未物化 Stage/C 创建或加入有界 single-flight Operation；Ticket 只等待该确切工作，不默认等待未来 Sweep。
- `inspect` 只读权威 Ticket Snapshot，不分配大 Buffer，也不改变 read-clear 状态。
- `open_read` 原子执行 Ready→Reading 和 ResultPin→ReaderLease；返回的 handle 是一次性授权能力。
- L1 只能通过 handle 的 bounded metadata 和只读 chunks 编码；不能取得内部 lease、文件路径或可写 Buffer。
- 编码完成、断线或 timeout 后必须调用 `finish_read`，它消费 handle，并同批完成 Reading→Consumed/Failed/Abandoned 与 ReaderLease 释放。兜底 TTL 只处理异常遗留，不代替正常 terminal。

### 5.3 initial_view/watch

```cpp
using WatchSubmission = Variant<
    WatchAccepted<WatchId>,
    WatchRejected<WatchError, ReclaimedWatchSinkRegistration>>;

using StopWatchResult = Variant<
    StopAccepted,
    AlreadyTerminal,
    StopRejected<WatchStopError>>;

using PreviewSubmission = Variant<
    PreviewAccepted<PreviewSubscriptionId>,
    PreviewRejected<PreviewError, ReclaimedPreviewSinkRegistration>>;

using StopPreviewResult = Variant<
    StopAccepted,
    AlreadyTerminal,
    StopRejected<PreviewStopError>>;
```

- `initial_view` 从一个授权 Catalog cut 返回状态与 `{catalog_revision,event_cursor,boot_id,event_epoch}`。
- `watch` 从 `event_cursor + 1` 重放并转实时；重放/实时交叠按 sequence 去重。
- boot/epoch 改变、cursor 超出 retention、显式 gap 或 access-set 变化返回 `ResnapshotRequired`。
- `WatchSinkRegistration` 是 move-only 的投递端与生命周期能力，不是跨异步时长保存的 `EventSink&`。Rejected 归还 registration 且零 callback；Accepted 后由 Kernel 持有到有序事件流和恰好一次 Watch terminal 完成。Socket 断线只停止 wire 编码，不销毁内部 registration，也不免除 terminal 义务。
- `stop_watch` 必须校验 owner/session/access；可猜的 `WatchId` 本身不是授权能力。`StopAccepted` 只表示发起停止，registration 继续保活到唯一 Watch terminal；`AlreadyTerminal` 表示 terminal 已完成；`StopRejected` 零 stop callback，原 Watch/registration 保持原状态并可继续交付，错误 ID/权限不能吞掉 owner。registration 析构也不是 stop。
- Event 只通知事实变化，不代替 Query pin、SCPI fence 或正式数据读取。

### 5.4 Preview：授权 mailbox，不是 L4 到 Web 的 callback

`begin_preview` 校验 actor/session/access、目标 Channel/Operation 和 ProductProfile；同步 Rejected 必须完整归还 `PreviewSinkRegistration` 且零 callback。Accepted 后由 Kernel/Runtime 的有界 `PreviewHub` 持有 move-only registration 直到唯一 stream terminal；L1 只消费 mailbox，不把 Socket、HTTP response 或 callback 引用交给 L4。`stop_preview` 的 `StopAccepted | AlreadyTerminal | StopRejected` 与 Watch 采用同一语义：Accepted 只发起停止，Rejected 零 stop callback 且原订阅/registration 保持有效，只有唯一 Preview terminal 后才能释放 registration。每轮 admission 向 L4 下传一个绑定 `OperationId + LogicalSweepId + generation + subject + profile revision + byte/tile/rate quota` 的 `AuthorizedPreviewPublisher`。L4 只能非阻塞 `try_publish(PreviewTile&&)`；没有订阅者或任一消费者队列满时可以丢 provisional tile并累计有界 drop 计数，正式 chunk/Builder 永不因此阻塞或丢失。

Hub 对每个订阅交付显式 generation begin、bounded tile、`PreviewGap{generation,dropped_count}` 与唯一 stream terminal。每个 consumer 绑定 access revision；session 过期或角色/ACL/access-set 发生任何扩大或缩小时，Hub 立即返回唯一 `SessionExpired/AccessChanged` terminal 并撤销该订阅，调用者必须重新获取授权状态后再订阅。Socket 断线只结束该消费者 registration，不取消 Sweep；producer 可在无订阅者时继续丢弃 tile。

`AuthorizedPreviewPublisher` 还绑定 `FormalReplacementPolicy`，明确该 provisional generation 最终对应 A、B 还是某个 C/Trace revision。L4 Acquisition run 成功时只能把 publisher 转成 move-only `PreviewFinalizationOwnerSet`，随 Acquisition typed terminal 返回 L2；A/Board terminal 绝不等于 formal result。只有 L2 收到目标 `InstrumentStore::commit` 成功 receipt 后，才能发送 `SupersededByFormalResult{TypedPublicationRef}`。candidate/commit/必达后继失败或取消时发送 `Discarded/Failed`；若目标是 B 或是依赖该新 B 的 C，A commit 后 L2 把 owner 装入 `RuntimeHeldPreviewEscrow` 随 WorkPermitSet 交给 L3。Runtime 在调用 MeasurementPipeline 期间持有它，不传入 L4，并在 worker terminal 后附加到 Runtime completion 或 Drain owner。B commit 后 B-target 直接终结，C-target 才进入下述 exact-C admission/fallback。

C/Trace 不是 RF start 门禁，且以本次新 B 为父的 C 在 B commit 前尚无正式 B ref，因此不能提前 pin。B commit 成功后，L2 在同一个有界 Control Executor turn 内独立尝试 `reserve_work → pin B/Stage refs + reserve output → commit Pending`；全部成功后 exact C job 才接管 C-target owner。任一步失败则立即按冻结的 `CPreviewFallback{SupersedeWithB | FormalUnavailable | Discarded}` 终结，不排队等待、不后台重试、不无限保活 PreviewHub 配额。Draining 则连同全部 Hub quota/queue/completion owner 转交 Drain。这些 stream terminal 都不代替 Operation terminal。Preview 永不进入 L5，不供 SCPI 正式数据、Marker、Limit、Calibration、Export 或 ResultPin 使用。

### 5.5 大文件上传与统一下载

`CommandEnvelope`/`QueryEnvelope` 始终是有界值。State、Touchstone、Cal Kit、Limit、Fixture 等上传先走独立 Binary Transfer lane：`begin_blob_write` 预留 actor/session 配额、staging slot、cleanup slot、deadline 和稳定 `BlobWriteCompletionRegistration`，返回 opaque `BlobWriteHandle`；Rejected 归还全部 move-only 输入且零 callback。`write_blob_chunk` 只接受固定池的 move-only `BlobChunkLease`，按 credit 非阻塞接管或原样归还，不能无限缓存；`finish_blob_write` 必须消费 handle，并产生 `StagedBlobRef | BlobWriteFailed | BlobWriteDraining{DrainId}`。Completed 分支校验声明长度、digest、media/schema hint 后才返回 actor/session/TTL 绑定的 `StagedBlobRef`；普通 Completed/Failed 返回已经是真实资源 terminal，registration 随之释放。无法中断的 flush/cleanup 则把 handle、已接管 chunks、staging、transfer quota、lane 和 completion owner 整体移入 Runtime/Persistence 的现有两段 Drain supervisor；`BlobWriteDraining` 后必须通过该 registration 对 `DrainId` 恰好交付一次 `DrainTerminal`，L2 据此归还配额和提交诊断。断线、timeout、TTL 或 handle 遗失也进入同一已预留 supervisor；析构只做非阻塞 fail-safe handoff，绝不冒充 cleanup 已完成。后续 Import/Recall Command 只引用该 ref，并再次校验 owner、purpose、digest 与单次消费/显式复用政策；任何 path、FD、Socket 或大 byte array 都不进入 Command。

下载不新增另一套 `BlobReadHandle`。Export/diagnostic 成功只返回待 L2 验证并提交的 `BlobResultRef` candidate；Ready QueryTicket 的 `QueryReadHandle` 是封闭的 snapshot/blob reader variant，仍由 `open_read` 原子取得、在 Binary Transfer lane 流式编码，并由 `finish_read(Consumed|Failed|Abandoned)` 消费和释放。Persistence/File Adapter 的路径、句柄和 `BlobReadLease` 只存在于该 opaque reader 的 Implementation 内部。

### 5.6 SweepAdmissionPlanner：首次 dispatch 前的纯规划 seam

Sweep Compiler 与保守资源声明不能藏在已经进入 Runtime 的 `AcquisitionEngine::run` 之后，否则 L2 无法在首次 dispatch 前取得输入 pin、下游容量和 Board pre-admission。本项目把它冻结为 **L2 InstrumentKernel Implementation 内部**的纯 planning Module；它不是 L2 绕过 Runtime 调用 L4 的捷径：

```cpp
struct FrozenSweepPlanningInput {
    SweepPurpose purpose;
    ChannelRevisionSnapshot channel;
    ProductProfileSnapshot profile;
    CapabilitySnapshotSet capabilities;
    ResourceTopologyRevision topology;
};

struct SweepAdmissionPlan {
    FrozenSweepJob job;                         // 内含 SweepIntent 与 plan digest
    WorkAdmissionClaim acquisition_work;
    RequiredPostAcquisitionWorkClaim continuation_work;
    TypedInputRefSet purpose_dependencies;
    ConservativeAcquisitionClaim resources;    // A/ingress/必达后继的最坏上界
};

class SweepAdmissionPlanner {
public:
    Result<SweepAdmissionPlan, SweepPlanningError> plan(
        const FrozenSweepPlanningInput& input) const noexcept;
};
```

`plan` 无副作用、无 I/O、不创建 Operation/lease、不调用 Board/Runtime/Store，且受 ProductProfile 的 boards/points/segments/ports/observation 数量硬上界约束。它从同一授权 `CatalogCut` 组装的冻结输入中合并 Live Measurement、Calibration/Verification 与显式导出需求；输出尽量保持符号化，不能在 Control Executor 上展开几十万点数组。输出的 job、Runtime claim、typed refs 和 conservative claim 共享一个不可变 plan digest，后续任一 revision/epoch/digest 不匹配都必须拒绝。若目标 Profile 下的 planner WCET 不能满足 Control/Safety ingress 延迟预算，则在配置 revision 生成时缓存 `SweepPlanRevision`，或在固定 Planning lane 纯计算后回到 L2 重验 expected revisions，不能长时间占住唯一 Control Executor。

`SweepAdmissionPlanner` 是纯 Module；`ResourceArbiter` 不是。后者维护当前 ResourceGraph 占用，按 canonical Board/Resource ID 顺序和同一 topology epoch 为单板或多板 **全有或全无**地签发排他 `PreAdmissionLease`。不能把“资源看起来可用”的纯校验冒充 lease，否则两个并发 Sweep 会同时驱动同一 source/route。

L2 的固定全局顺序是：先 `plan`；再分别为 acquisition 与 purpose-specific 必达后继调用 Runtime `reserve_work`；从 InstrumentStore 原子 pin 全部依赖、预留输出，并为将要可见的 Operation/Ticket 取得 `LifecycleTerminalReservationSet`；Query Pending admission 还要为该调用者取得独立 `PendingResultPinReservation`；最后由 stateful ResourceArbiter 按 conservative claim/topology epoch `try_pre_admit`。这些调用必须非阻塞或有严格短上界，不能持前一个 lease 阻塞等待后一个。A-only purpose 没有后继 work claim。任何一步失败都按相反顺序释放此前本地 permit/lease，不创建幽灵 Operation。

全部成功后，owner 先聚合为一个 move-only 对象，而不是散落在局部变量中：

```cpp
struct ReservedWorkDispatch {
    WorkId work_id;
    WorkDispatchPermit permit;
    RuntimeCompletionRegistration completion; // 已绑定 WorkId 与固定 Control mailbox slot
};

struct PendingSweepAdmission {
    OperationId operation_id;          // 已分配，commit 前不可见
    FrozenSweepJob job;
    ReservedWorkDispatch acquisition_work; // permit + reliable completion registration
    AcquisitionLeaseSet acquisition;       // capacity 内含必达后继的 ReservedWorkDispatch
    LifecycleTerminalReservationSet terminal_facts; // 初始 commit 后转入 L5
};
```

它只允许 `Building → CommittedNotDispatched → RuntimeOwned → WorkerOrDrainOwned → CandidateCommitOwned | Released`。Operation 初始 commit 成功时 execution lease 原子进入 `CommittedNotDispatched`，terminal reservation 则安装进 L5 并跟随可见 lifecycle；初始 commit 失败时 Store 不创建 Operation、归还或释放 Store reservation，L2 释放完整本地 admission owner，绝不 dispatch。dispatch 消费 execution 整体及已预留 permit，不得再因普通 Busy 拒绝。commit 后若 token/digest 等内部契约错误导致 dispatch 失败，L2 使用已安装的 terminal reservation 提交该 Operation 的 Failed terminal，再释放归还的完整 owner。L4 只消费已经冻结的 job/leases，在 actual Manifest 出现后于既有 envelope 内本地收窄，绝不重新编译 current Catalog、换板、扩大 observation plan 或申请新资源。Continuous/Groups 每个 child Sweep 都重新取得 cut、plan 和 admission，父 Operation 不永久持有未来轮次的硬件/内存/worker lease。

## 6. L2 → L3：OperationRuntime Interface

```cpp
class OperationRuntime {
public:
    virtual Result<ReservedWorkDispatch, RuntimeAdmissionError>
    reserve_work(const WorkAdmissionClaim& claim) noexcept = 0;

    virtual DispatchResult dispatch(
        FrozenWorkItem&& work,
        WorkPermitSet&& permits,
        RuntimeCompletionRegistration&& completion) noexcept = 0;

    virtual StopRequestResult request_stop(WorkId work,
                                           const StopRequest& request) noexcept = 0;

    virtual DrainView inspect_drain(DrainId drain) const noexcept = 0;
};

using DispatchResult = Variant<
    DispatchAccepted,
    DispatchRejected<ReclaimedFrozenWorkItem,
                     ReclaimedWorkPermitSet,
                     ReclaimedRuntimeCompletionRegistration>
>;
```

`dispatch` 是 L2 看到的唯一派发入口，Implementation 内部按 typed work variant 选择固定 Acquisition/Processing/Solver/Persistence/Diagnostics lane。Interface 不暴露线程池、队列或 future。

`reserve_work` 是同步、有硬上界的 lane/queue/completion admission，不创建 Operation、不运行工作，也不读取领域对象。成功返回的 `ReservedWorkDispatch` 显式分配 `WorkId`，并同时保留 dispatch capacity 和绑定该 ID 的可靠 completion mailbox/registration；permit、registration、后续 `FrozenWorkItem` digest 与 `dispatch` 都必须匹配同一 ID，L2 用它建立 Operation↔Work 映射并调用 `request_stop`。后续构造 completion 不再有可失败的固定池申请。L2 的固定顺序是：先取得该整体，再从 L5 取得全部 input/output lease 和其他资源授权，全部成功后才提交 Accepted/Pending 事实并调用 `dispatch`。任一前置步骤失败都释放本地 reserved dispatch 且不留下幽灵 Operation/Ticket。`dispatch` 消费 permit/registration 后不得再因普通队列或 completion 容量 Rejected；若 token/digest/WorkId 不匹配等契约错误发生在事实已提交后，它归还全部输入，L2 必须提交可查询 Failed 终态。

### 6.1 dispatch 规则

- Rejected：Runtime 未接管 work/permits/registration，不产生 completion；`DispatchRejected` 原样归还全部 move-only 输入，不允许出现传参已经移动但无人释放的含糊状态。
- Accepted：Runtime 接管全部 work/permits/registration；registration 对该 `WorkId` 恰好收到一次 `RuntimeWorkCompletion`。它拥有稳定 mailbox/dispatcher 到真实资源终态，不依赖调用栈对象、协议连接或裸 callback 引用的寿命。
- progress 有界、可合并、可丢弃，不承担完成语义；completion 不可丢。
- Runtime 不读取 current Catalog，不解释 Command/Profile，不生成领域 patch。
- completion callback 不在 worker 资源锁或 L5 锁内调用；L2 在自己的 Control Executor 上处理。

### 6.2 cancel、deadline 与 Drain

`request_stop` 的直接结果只表示请求是否送达。协作工作在有界检查点退出；不可中断调用不能让父 Operation 假装仍占用零资源。

Runtime completion 是以下有类型 variant：

```cpp
using RuntimeWorkCompletion = Variant<
    PublishableTerminal,     // success + optional candidate batch
    FailedTerminal,          // true resource terminal, no publication
    DrainingHandoff          // parent can become visible terminal; resources stay in Runtime
>;

using DrainTerminal = Variant<
    DrainedTerminal,         // cleanup completed; named resources released
    QuarantinedTerminal,     // named resources/session remain isolated
    DrainCleanupFailed       // cleanup ended unsuccessfully with retained-ownership evidence
>;
```

进入 `DrainingHandoff` 时：

- L2 可以把父 Operation 提交为 TimedOut/Cancelled/Failed；
- Runtime 继续拥有 lane、input pin、output reservation、临时文件/SDK call 和预算；
- 迟到成功结果一律 abort，不得发布；
- 同一个 `RuntimeCompletionRegistration` 继续保活，并在该 `DrainId` 上恰好再交付一次 `DrainTerminal`；L2 据此提交 ResourceGraph、Board health/quarantine、诊断和审计变化。L3 不能直接修改 L5；
- `inspect_drain` 只用于重连/恢复时读取权威快照，不是可靠 completion 通道，也不能代替 `DrainTerminal`；
- 卡死或不安全硬件进入 Quarantine，terminal 必须列出仍被谁拥有的资源和重新准入条件；
- 不得补建无限 worker 来伪造容量恢复。

因此 Runtime 是显式的两段完成协议：非 Drain 路径只有一个 work terminal；Drain 路径先有一个 work handoff，再有一个资源 terminal。只有后者到达后，registration 才可释放。即使原 Web/SCPI 请求已经断线，这条内部 completion 链也必须继续。

## 7. L3 → L4：领域执行 Interface

三类执行入口都返回封闭 terminal variant，不能用“空 batch + bool”含糊表达 failure，也不能在仍持有资源时返回普通失败：

```cpp
struct ContinuationStoreJoinOwner {
    PinnedInputSet frozen_dependencies;
    OutputReservation successor_output;
    ContinuationJoinReservation new_a_join; // 预留 A extension pin/closure/quota bookkeeping
    PlanDigest plan_digest;
};

struct ContinuationRuntimeEscrow {
    ReservedWorkDispatch reserved_dispatch;
    PlanDigest plan_digest;
};

struct RequiredContinuationOwner {
    ContinuationStoreJoinOwner store;
    ContinuationRuntimeEscrow runtime;
};

using AcquisitionContinuationOwner = Variant<
    MeasurementPublicationContinuationOwner,   // 普通 Channel：B 必达
    CalibrationObservationContinuationOwner,   // 标准件必达；独立闭包
    CalibrationVerificationContinuationOwner,  // 验证必达；目标 Set/比较预算
    AuthorizedAOnlyCompletionOwner              // 仅显式 raw/diagnostic workflow，无 Store/Runtime 后继
>;

struct AcquisitionSucceeded {
    PublicationCandidateBatch candidates;       // A candidate
    AcquisitionContinuationOwner continuation;  // 与 FrozenSweepJob purpose 匹配
    PreviewFinalizationOwnerSet previews;        // 目标 formal commit 前继续保活
};

struct ProcessingSucceeded {
    PublicationCandidateBatch candidates;       // B、Stage 或 C candidate batch
};

struct RuntimeHeldPreviewEscrow {
    PreviewFinalizationOwnerSet previews;
    WorkId work_id;
    PlanDigest plan_digest;
}; // L3 跨 L4 调用持有，不是 MeasurementPipeline 输入

struct CalibrationSucceeded {
    PublicationCandidateBatch candidates;       // Observation/Set/Verification candidate
};

struct AcquisitionFailed {
    AcquisitionError error;
    ResourceTerminalEvidence resources;
    PreviewFinalizationOwnerSet previews;        // 由 L2 在失败事实提交后终结
};

struct ProcessingFailed {
    ProcessingError error;
    ResourceTerminalEvidence resources;
};

using AcquisitionTerminal = Variant<
    AcquisitionSucceeded,
    AcquisitionFailed,       // 执行资源已释放；Preview owner 显式归还 L2
    AcquisitionDraining      // move-only AcquisitionDrainOwner（含 Preview）+ DrainId
>;

using ProcessingTerminal = Variant<
    ProcessingSucceeded,
    ProcessingFailed,        // 执行资源已释放；Preview escrow 仍在 Runtime
    ProcessingDraining       // move-only ProcessingDrainOwner + DrainId；Runtime 再合并 Preview escrow
>;

using CalibrationTerminal = Variant<
    CalibrationSucceeded,
    CalibrationFailed,       // ResourceTerminalEvidence：所有 owner 已释放
    CalibrationDraining      // move-only CalibrationDrainOwner + DrainId
>;

using PersistenceTerminal = Variant<
    PersistenceSucceeded,    // typed staging/load/export/blob result
    PersistenceFailed,       // ResourceTerminalEvidence：所有 owner 已释放
    PersistenceDraining      // move-only PersistenceDrainOwner + DrainId
>;

using DiagnosticsTerminal = Variant<
    DiagnosticsSucceeded,    // typed self-test/bundle result
    DiagnosticsFailed,       // ResourceTerminalEvidence：所有 owner 已释放
    DiagnosticsDraining      // move-only DiagnosticsDrainOwner + DrainId
>;
```

Acquisition Succeeded 把 A candidate/continuation/preview-finalization 的全部 owner 交给 Runtime；Acquisition Failed 携带已释放执行资源的证据且不含 candidate，同时显式归还 Preview owner 供 L2 在失败事实提交后终结。Processing worker 不接收或透传 Preview owner；L3 在调用其 `run` 期间持有 `RuntimeHeldPreviewEscrow`，并在 worker success/failed 后附加到给 L2 的 Runtime terminal，或在 Draining 时与 input/output/lane/预算/算法工作整体合并到具名 Drain owner。Runtime 将这些分支转换成 `PublishableTerminal`、`FailedTerminal` 或 `DrainingHandoff`，并负责后续唯一 `DrainTerminal`；L4 不能自己更新 L5，也不能宣布 formal Preview replacement。Continuation variant 必须与冻结的 `FrozenSweepJob::purpose` 精确匹配：校准采集不能借普通 Measurement permit 偷 pin 当前用户 Correction/accumulator，普通 Channel 也不能伪装成 A-only 绕过 B 发布门禁。

### 7.1 AcquisitionEngine

```cpp
struct AcquisitionLeaseSet {
    PreAdmissionLease board_pre_admission;
    AcquisitionContinuationOwner continuation;
    ConservativeAcquisitionCapacityEnvelope capacity;
    PreReservedBoardCallSet board_calls; // 逐板 prepare/run call slot + sink registration
    ExactFinalizationCapability finalize;
    AuthorizedPreviewPublisher preview;
};
```

该集合在第一次 Runtime dispatch 前已完整取得：purpose dependencies 已 pin，A Builder/ingress 与必达后继的 queue/worker/output 按 Intent、Profile、Capability 上界保守占位；非 A-only continuation 的 Store join owner 与 `ContinuationRuntimeEscrow{ReservedWorkDispatch}` 已组合，Preview publisher 也已绑定 Operation/generation 和硬配额。`PreReservedBoardCallSet` 为每块板预留 prepare/run Adapter call/worker/queue slot 与 `PrepareSinkRegistration`/`BoardRunSinkRegistration`，因此 Operation commit 后不再临时 acquire 这些容量。

stateful ResourceArbiter 在 pre-admission 时已签发并封装 `ExactFinalizationCapability`；prepare 得到 actual Manifest 后，L4 只在本地消费它，在该 envelope 内校验并派生 exact `AcquisitionRunResourceSet + StartAuthorization`。此处不回调 ResourceArbiter/L2/L5/Runtime，不申请新容量，也不能扩大 claim。Manifest 超界或无法在 envelope 内成形时，显式 discard Prepared token，等待 cleanup terminal 后返回 AcquisitionFailed。

```cpp
class AcquisitionEngine {
public:
    virtual AcquisitionTerminal run(FrozenSweepJob&& job,
                                    AcquisitionLeaseSet&& leases,
                                    ExecutionContext& context) noexcept = 0;
};
```

Implementation 隐藏 actual Manifest validation/finalization、Composite Coordinator、Board prepare/start、chunk ledger、A Builder、Preview tap、abort/safe-state 收尾；Sweep Compiler 与 conservative admission 已在 L2 planning 阶段完成。成功只返回包含 A candidate 的 `PublicationCandidateBatch`；失败或取消不发布部分 A。多板时 Engine 持有全组 `AcquisitionRunResourceSet`，其中逐板绑定 Manifest、board execution sublease、BoardRunId 和 all-terminal barrier；单个 Board Adapter 不参与组合决策。

实际 Manifest 出现后的资源交接必须闭合，不能把 L4/L5 的 reservation 塞进一个随后移给 L6 的授权 token：

1. 第一次 Runtime dispatch 前，L2/L3 已按 `FrozenSweepJob::purpose` 原子取得保守 envelope 与必达后继：普通测量 pin 旧 accumulator、CorrectionSet 和 B graph/Profile 闭包并预留 B queue/worker/output；校准 Observation pin CalibrationSession/Method/Standard 与独立 average closure，明确排除当前用户 Correction/DUT B；校准 Verification pin 目标 Correction 与比较预算；显式 raw/diagnostic 才能使用受策略授权的 A-only。Manifest 出现后 L4 只在 envelope 内收窄成 move-only `AcquisitionRunResourceSet`；按需 Stage/C 各自在正式父结果发布后重新 admission，不属于普通 RF start 门禁；
2. Resource Arbiter 在 pre-admission 时签发 `ExactFinalizationCapability`；L4 收到 Manifest 后在本地消费它派生 `StartAuthorization`。该 authorization 只携带绑定 reservation ID/digest 的不可伪造证明和板侧执行权，不拥有 processing/output reservation；
3. ingress owner 派生一个 `RunDeliveryGrant` 给 L6。该 grant 只授予一个 producer 向已预留 ingress 移动 chunk 的能力，不能释放、扩容或取得 backing storage；L4 仍持有 owner；
4. `begin_run` Rejected 时归还 start token、authorization、delivery grant 和 sink registration；Accepted 时 L6 只持有 producer grant 到 run terminal，L4 持有 resource set、Manifest 和 ledger 到 Builder terminal；
5. 成功时 A 的 sealed Buffer/candidate lease 与完整 `AcquisitionContinuationOwner` 随 typed terminal 经 L3 返回 L2；非 A-only variant 内含 StoreJoinOwner + RuntimeEscrow，`AuthorizedAOnlyCompletionOwner` 不创建空 handoff。失败时在 run/safety/all-terminal 后释放，无法中断时把完整 resource set 原子转交 Drain；
6. `AcquisitionEngine::run` 返回后，任何 reservation 都必须已经位于 candidate、下一工作 permit、已释放状态或具名 Drain owner 中，不允许留在局部临时对象或由 L6 反向持有。

### 7.2 MeasurementPipeline

```cpp
class MeasurementPipeline {
public:
    virtual ProcessingTerminal run(FrozenProcessingJob&& job,
                                   PinnedInputSet&& inputs,
                                   OutputReservation&& output,
                                   ExecutionContext& context) noexcept = 0;
};
```

`FrozenProcessingJob` 是 `BuildMeasurement | MaterializeStage | EvaluateAnalysis` 的封闭 variant。成功批可以包含相互依赖的 B + accumulator，或 C + TraceEvaluation + MarkerEvaluation + LimitResult。Pipeline 不读取 current Channel/Trace，不更新 Head，不发布 Event；Eigen 和所有私有缓存留在 Implementation。

### 7.3 CalibrationModule

```cpp
class CalibrationModule {
public:
    virtual CalibrationTerminal run(FrozenCalibrationJob&& job,
                                    PinnedInputSet&& inputs,
                                    OutputReservation&& output,
                                    ExecutionContext& context) noexcept = 0;

    virtual Result<CorrectionMatchReport, CalibrationError>
    match(const CorrectionSetView& correction,
          const PreparedExecutionManifestSet& execution) const noexcept = 0;
};
```

`FrozenCalibrationJob` 覆盖 AcceptObservation、Solve 和 Verify，但 Calibration Session 的步骤推进、选择与 commit 仍归 L2。`match` 是有硬上界的纯计算，必须逐板评估非空 Manifest set；若真实实现需要 I/O，它就不再满足该 Interface，必须改造成 Runtime work。

### 7.4 Persistence 与 Diagnostics

```cpp
class PersistenceModule {
public:
    virtual PersistenceTerminal run(
        FrozenPersistenceJob&& job,
        TypedSnapshotLeaseSet&& inputs,
        PersistenceOutputReservation&& output,
        ExecutionContext& context) noexcept = 0;
};

class DiagnosticsModule {
public:
    virtual DiagnosticsTerminal run(
        FrozenDiagnosticsJob&& job,
        TypedSnapshotLeaseSet&& inputs,
        DiagnosticsOutputReservation&& output,
        ExecutionContext& context) noexcept = 0;
};
```

空输入也用有类型的 empty lease set，不改成借用裸引用。Persistence Succeeded 只返回 staging/load/export/blob typed result；Diagnostics Succeeded 只返回自检/诊断包 typed result。任何会改变在线 Instrument/Channel/Calibration/Analysis/Display Catalog 的结果，都由 L2 重新校验并装入 `DomainCommitBundle`。文件 rename、压缩、SDK probe 等不可中断调用必须把 input leases、temp/output reservation、lane、预算和最终 completion 能力一起移入对应 Drain owner；不能从一个只借用 `const&` 的入口声称完成所有权转交。

## 8. L2 → L5：权威事实 Interface

L5 可以由几个内部 Module 实现，但 L2 只依赖一个深的事实/所有权表面：

```cpp
using OpenResultReadResult = Variant<
    ReadOpened<QueryReadHandle>,
    ReadOpenRejected<StoreError,
                     ReclaimedQueryReadAuthorization,
                     ReclaimedReaderPermit>
>;

struct ContinuationStoreJoinRequest {
    CandidateLocalRef new_a;
    ContinuationStoreJoinOwner owner;
};

struct ContinuationStoreHandoff {
    TypedPublicationRef new_a;
    PinnedInputSet complete_inputs;       // 新 A + 冻结 dependencies
    OutputReservation successor_output;
    PlanDigest plan_digest;
};

struct CommitSucceeded {
    CommitReceipt receipt;
    ContinuationStoreHandoffSet continuation_store_handoffs;
};

struct CommitFailed {
    StoreError error;
    DomainCommitAbortReceipt consumed_owners; // candidate/Store join owner 已消费或释放
};

using CommitResult = Variant<CommitSucceeded, CommitFailed>;

struct ReadFinishReceipt {
    QueryTicketId ticket;
    QueryTicketRevision revision;
    ReadTerminal terminal;
};

class InstrumentStore {
public:
    virtual Result<CatalogCut, StoreError>
    read_catalog(const CatalogReadRequest& request,
                 const CatalogReadPermit& permit) const noexcept = 0;

    virtual Result<PinnedInputSet, StoreError>
    pin_inputs(const TypedInputRefSet& refs,
               InputPinPermit&& permit) noexcept = 0;

    virtual Result<OutputReservation, StoreError>
    reserve_outputs(const OutputClaim& claim,
                    OutputReservePermit&& permit) noexcept = 0;

    virtual Result<LifecycleTerminalReservationSet, StoreError>
    reserve_lifecycle_terminals(
        const LifecycleTerminalClaimSet& claims,
        LifecycleTerminalReservePermit&& permit) noexcept = 0;

    virtual Result<PendingResultPinReservation, StoreError>
    reserve_pending_result_pin(
        const PendingResultPinClaim& claim,
        ResultPinReservePermit&& permit) noexcept = 0;

    virtual CommitResult commit(DomainCommitBundle&& bundle,
                                DomainCommitPermit&& permit) noexcept = 0;

    virtual OpenResultReadResult open_result(
        QueryTicketId ticket,
        QueryReadAuthorization&& authorization,
        ReaderPermit&& permit) noexcept = 0;

    virtual ReadFinishReceipt finish_result(
        QueryReadHandle&& handle,
        ReadTerminal terminal) noexcept = 0;

    virtual EventFeedSubmission begin_event_feed(
        const EventFeedRequest& request,
        EventFeedPermit&& permit,
        EventFeedRegistration&& registration) noexcept = 0;

    virtual StopEventFeedResult stop_event_feed(
        EventFeedControlHandle&& control) noexcept = 0;
};

```

### 8.1 commit 是唯一可见性边界

`DomainCommitBundle` 可以包含：

- 可选 `PublicationCandidateBatch`；
- 有类型 `DomainCatalogPatchSet`；
- Head、Operation/fence、Instrument Status、SCPI Session State；
- WaitRegistry、QueryTicket/ResultPin；
- 初始 Accepted/Pending commit 可安装 `LifecycleTerminalReservationSet`；Pending Query 可把 caller-specific `PendingResultPinReservation` 装入 Ticket；
- 可选 `ContinuationStoreJoinRequestSet`：把本批新发布的 A candidate 与 move-only、purpose-specific `ContinuationStoreJoinOwner` 原子合并；
- Event batch 与 retention delta。

commit 规则：

1. permit 只由 L2 唯一 Control Executor 获得，并绑定预期 Catalog cut；
2. 任何异步 Operation、Pending Query 或 Drain lifecycle 在首次可见前都必须取得并在同一初始 commit 中安装 `LifecycleTerminalReservationSet`；可进入 Draining 的 work claim 同时预留有界 contingency child Drain fact/terminal slot，handoff 时原子安装，未使用则在父 terminal 释放；初始 commit 失败不产生 lifecycle，也不得 dispatch；
3. bundle 内所有 expected revision、typed refs、closure、quota 和 patch 先完整验证；
4. 成功时一次发布并消费 candidate lease；若带 Store join request，则同一事务把新 A 与 owner 中已 pin 的冻结依赖合并成完整 `PinnedInputSet`，通过 `CommitSucceeded::continuation_store_handoffs` 返回 Store-owned handoff；L2 再与始终留在 commit 外的 `ContinuationRuntimeEscrow` 组合后才能派发 BuildMeasurement 或 Calibration Observation/Verification；A-only 不创建空 handoff；
5. 失败时所有 Catalog/Head/Ticket/Event 保持旧状态，并沿唯一 abort 路径消费/释放 candidate；调用者不能在失败后继续访问已移动 batch；
6. 若失败 bundle 之前已有可见 Operation/Ticket，L5 中原有 terminal reservation 因事务全败而仍有效。L2 必须在同一有界 Control turn 内 reconcile 已由 cancel/timeout 提交的终态，或用该 reservation 提交不带 candidate 的 state-only failure bundle，使 Operation/Ticket、Status、Wait/Fence 与失败 Event 原子进入 Failed；普通 quota、revision 或队列容量不得让这次 terminalization 失败，只有 Store 完整性故障允许 Instrument 进入 fail-stop；
7. callback/wakeup 只在锁外根据 receipt 投递；EventJournal 不 pin 大数据；
8. 已发布 Snapshot 永不原地修改，Head 只选择 last-good/current relation。

`AcquisitionContinuationOwner` 从 RF start 前一直保活对应 purpose 的冻结依赖和必达下游容量；A candidate 的 Buffer 则由 `CandidateCommitLease` 保活。A commit 前，L2 将它拆成 `ContinuationStoreJoinOwner` 与 `ContinuationRuntimeEscrow`：只有前者随 bundle 进 Store，后者在同步有界 commit 期间仍由 Control Executor 持有。`ContinuationJoinReservation` 在 RF start 前就按保守 A closure 上界预留新 A 的 pin count/bytes/closure node/quota bookkeeping，因此 Store 事务原子安装 A 并形成完整 input closure 时不再发生普通容量申请。这样不存在“先发布 A、再尝试 pin A/旧依赖”的 retention 窗口。A commit 失败时 Store 消费/释放 candidate 与 Store join owner，L2 恰好一次释放 Runtime escrow并终结 Preview，然后使用 SweepOperation 已安装的 terminal reservation 提交 Failed；不发布 A，也不留 Pending/Publishing Operation。

`DomainCommitBundle` 和 `CommitResult` 的类型图中禁止出现 `ReservedWorkDispatch`、`WorkDispatchPermit`、`RuntimeCompletionRegistration` 或 `PreviewFinalizationOwnerSet`。`WorkId`/purpose/digest 可作为不可执行的关联事实写入 Operation patch，但 L5 不接收、保存或返回 Runtime/Preview capability，也不建立 L5→L3 类型依赖。

`PreviewFinalizationOwnerSet` 不进入 L5，也不影响 commit 原子性；Control Executor 在同步有界的 commit 调用期间与 Runtime escrow 一起继续持有它。receipt 成功后，目标为本次 publication 的 owner 才以其 typed ref 发送 `SupersededByFormalResult`，目标为后继 B 或依赖该 B 的 C 的 owner 则装入 `RuntimeHeldPreviewEscrow`，与 `ContinuationStoreHandoff + ContinuationRuntimeEscrow` 同时交给 L3，但不传入 L4 MeasurementPipeline；B commit 后 B-target 终结，C-target 按 §5.4 独立 admission 或立即 fallback。commit 失败则发送 Discarded/Failed。不得在调用 commit 前释放或终结它。

### 8.2 Pending ResultPin：每个 waiter 独立预留

加入 materialize/evaluate single-flight 的每个 Query，在创建 Pending Ticket 前按 `{actor, session, target, output-claim upper bound}` 独立取得 `PendingResultPinReservation`；配额不足的调用者在此处被拒绝，不加入共享 Operation。Pending commit 成功后 reservation 留在该 Ticket 内并计入 global/per-actor/session quota。共享 publication 完成时，Control Executor 只选择同一权威 cut 上仍为 Pending 的 Ticket；Store 在同一个 publication bundle 内把它们各自的 reservation 转成精确 `ResultPinLease` 并释放上界余量。已经 cancel/expired/access-revoked 的 Ticket 先在自己的 terminal commit 中释放 reservation，不进入 Ready 集合。实际 `ResultClosure` 超出冻结 claim 是 Operation 的 `ResourceClaimExceeded`，不是向其他 waiter 借配额。

因此一个调用者的配额不足、取消或 deadline 只终止自己的 Ticket，不回滚共享 publication，不使其他 Ticket 失败，也不改变共享 Operation 的孤儿政策。direct Ready 没有 Pending reservation：它在创建 Ticket 的同一 state-only commit 中检查精确 closure 配额；失败同步返回 Rejected 且不创建 Ticket。

### 8.3 pin、reservation 和 reader

- `pin_inputs` 对完整 `TypedInputRefSet` 原子成功或失败；不能逐个 pin 后把半套交给 worker。
- `reserve_outputs` 覆盖正式 payload、结构共享 bookkeeping 和算法声明的峰值工作区。
- `ResultPinLease` 覆盖自包含 ResultClosure，不只是顶层 publication ID。
- Ready Ticket 与其 `ResultPinLease` 一直共同保存在 Store。L2 的 `InstrumentKernel::open_read` 只构造绑定 actor/access/profile/ticket revision 的 `QueryReadAuthorization`，不会先把 pin 取到进程栈上。
- `open_result` 在一次 Store 事务中验证 ticket/authorization，执行 `Ready → Reading`，并把该 Ticket 内的 `ResultPinLease` 原子转换成返回 handle 内的 `ReaderLease`。Rejected 不改变 Ticket，并归还 authorization/permit；不得先释放 pin 再申请 reader。
- `open_result` 同时从预留池取得对应的 finish/cleanup slot，因此 `finish_result` 是无异步分支、有硬上界的本地 Store 事务：它始终消费 handle，同批完成 `Reading → Consumed | Failed | Abandoned`、ReaderLease 释放和审计 receipt。正常编码完成、Socket 断线、timeout 和 codec failure 都必须显式调用；handle 析构不是成功终态。
- `ReadFinishReceipt` 只报告已经提交的 Ticket revision/terminal，不存在 Deferred 或“失败但 handle 是否仍有效”不明确的返回。Store 持久 I/O 不在该短事务内；若 Store 自身完整性已损坏到无法执行预留 cleanup，则 Instrument 进入 fail-stop/diagnostic fault，而不是继续运行并遗留一个无人观察的 Reading Ticket。
- `cancel_query` 在 Reading 前可以通过领域 commit 终止 Ticket；已经 Reading 时不能撤销 codec 正在使用的 ReaderLease，只能请求/等待上述 finish。TTL 只兜底遗留 registration，不替代正常 finish。
- Store 内部可以替换内存、文件或结构共享策略，但 Interface 的 closure/terminal 语义不变。

### 8.4 EventJournal replay → live feed

`InstrumentKernel::begin_watch` 不能绕过唯一 Store boundary 读取内部 EventJournal。L2 在校验 Watch actor/session/access/filter 后构造绑定 `{boot_id,event_epoch,catalog_revision,start_cursor,visibility envelope}` 的 `EventFeedPermit`，再调用 `InstrumentStore::begin_event_feed`。Store 在一个有界动作中确定 replay cut 并注册 live feed，保证从 `start_cursor` 到实时序列无窗口；Accepted 返回 move-only `EventFeedControlHandle`，并由 `EventFeedRegistration` 交付有序 bounded `EventRecord`、显式 `Gap/ResnapshotRequired` 和唯一 feed terminal。Rejected 归还 permit/registration 且零 callback。

`StopEventFeedResult` 是封闭结果：

```cpp
using StopEventFeedResult = std::variant<
    StopAccepted,
    AlreadyTerminal,
    StopRejected<ReclaimedEventFeedControlHandle>>;
```

`stop_event_feed` 移入 control handle；`StopAccepted` 只表示停止请求已接收，不是 feed terminal，原 `EventFeedRegistration` 及它的预留 terminal slot 继续保活到唯一 terminal。`AlreadyTerminal` 表示该 feed 的 terminal 已经完成。`StopRejected` 必须完整归还 move-only control handle 和错误，调用者不得丢失控制权。同一 Store 签发且仍有效的合法 handle 不得因普通容量、队列满或 access 变更而 Rejected；此类情形必须利用 begin 时预留的 stop/terminal 容量进入 Accepted。Rejected 只能表示伪造、错误 Store/epoch 等 capability 契约错误。

L2 把内层 feed 投影到外层 `WatchSinkRegistration`，继续执行对象 ACL/filter 与 access-revision 检查；L5 不解释 Web/SCPI session。stop/access change/shutdown 通过上述封闭结果发起停止，accepted 仍不等于 terminal，直到内层 feed terminal 后外层 Watch 才 terminal 并释放两套 registration。Event feed 只提供 metadata/typed refs，不 pin payload，也不能代替 Query、WaitRegistry 或 completion registration。

## 9. L4 → L6：BoardPort seam

BoardPort 是真实 seam：Real、Mock、Replay 至少三个 Adapter 满足同一个 `BoardProvider → OpenedBoard{Execution,Safety,Maintenance}` 契约。AcquisitionEngine 是唯一业务调用者；合同测试也是通过相同的权限分面 Interface 观察行为。

本层只在此冻结以下跨层原则，字段级契约见 [Board Adapter 契约](board-adapter-contract.md)：

- prepare 与 start 分离，请求值与 actual Manifest 分离；
- Start accepted 后，chunk/phase/唯一 terminal 由 `BoardRunSink` 交付；
- wrong Manifest/Prepared/Run/generation、重复 terminal 或 terminal 后回调锁存为类型化 `BoardContractViolation`；callback 只移动 lease 和记录有界事实，L4 在 callback 返回后的 Runtime 步骤隔离当前 Session；
- abort accepted 不等于 run terminal；
- RF safe-state 通过独立 SafetyPort/SafetyLane 和可信 readback 证明；
- `AcquisitionChunkLease` move-only；正式 chunk 不可丢，Preview 才可丢；
- 一 Session 只代表一块板；多板编排属于 AcquisitionEngine；
- Session 析构不是 abort、RF-off 或 recover。

## 10. 一次 Single Sweep 穿过 Interface 的完整踪迹

```mermaid
sequenceDiagram
    participant P as "L1 Web/SCPI Adapter"
    participant K as "L2 InstrumentKernel"
    participant S as "L5 InstrumentStore"
    participant R as "L3 OperationRuntime"
    participant A as "L4 AcquisitionEngine"
    participant B as "L6 OpenedBoard.Execution"
    participant M as "L4 MeasurementPipeline"

    P->>K: "submit(StartSweep CommandEnvelope)"
    K->>S: "read one authorized CatalogCut"
    S-->>K: "frozen Channel/Profile/Capability/Topology input"
    K->>K: "SweepAdmissionPlanner.plan; no side effects"
    K->>R: "reserve_work acquisition + required successor claims"
    R-->>K: "ReservedWorkDispatch set"
    K->>S: "pin purpose deps + reserve outputs + lifecycle terminal"
    S-->>K: "Store owners + LifecycleTerminalReservationSet"
    K->>K: "ResourceArbiter pre_admit + assemble AcquisitionLeaseSet"
    K->>S: "commit Operation Accepted + install terminal reservation"
    alt "initial commit failed"
        S-->>K: "CommitFailed; no Operation"
        K->>K: "release complete admission owner; no dispatch"
        K-->>P: "Rejected; no OperationId"
    else "initial commit succeeded"
        S-->>K: "CommitReceipt"
        K->>R: "dispatch FrozenSweepJob + WorkPermitSet + RuntimeCompletionRegistration; retain WorkId mapping"
        K-->>P: "Accepted OperationId"
        R->>A: "run(job, leases, context)"
        A->>B: "prepare(intent, derived PrepareAuthorization, pre-reserved sink)"
        B-->>A: "PreparedExecution + actual Manifest"
        A->>A: "validate + zero-allocation exact finalization; L4 retains owners"
        A->>B: "start(StartAuthorization + RunDeliveryGrant + pre-reserved sink)"
        B-->>A: "Accepted"
        B-->>A: "move chunks / phases / unique terminal"
        A-->>R: "A candidate + AcquisitionContinuationOwner + PreviewFinalizationOwnerSet"
        R-->>K: "PublishableTerminal"
        K->>K: "split StoreJoinOwner / RuntimeEscrow; keep escrow + Preview"
        K->>S: "commit A + ContinuationStoreJoinRequest"
        alt "A commit failed"
            S-->>K: "CommitFailed + abort receipt"
            K->>K: "release RuntimeEscrow; retain Preview until failure fact"
            K->>S: "state-only SweepOperation Failed via terminal reservation"
            S-->>K: "Terminal CommitReceipt | AlreadyTerminal"
        else "A commit succeeded"
            S-->>K: "CommitSucceeded + ContinuationStoreHandoff"
            K->>K: "assemble handoff + escrow; retain WorkId mapping"
            K->>R: "dispatch BuildMeasurement + WorkPermitSet + RuntimeCompletionRegistration; retain WorkId mapping; Runtime holds Preview escrow"
            R->>M: "run(job, inputs, output, context)"
            M-->>R: "B candidate"
            R-->>K: "PublishableTerminal"
            K->>S: "commit B + Heads + Sweep terminal + Event"
            alt "B commit failed"
                S-->>K: "CommitFailed + abort receipt"
                K->>S: "state-only SweepOperation Failed via terminal reservation"
                S-->>K: "Terminal CommitReceipt | AlreadyTerminal"
            else "B commit succeeded"
                S-->>K: "CommitSucceeded + B TypedPublicationRef"
                K->>K: "finalize B Preview; bounded exact-C admission or frozen fallback"
            end
        end
    end
```

关键检查：

- L1 得到的是 Operation，不是数组；
- planning、Runtime/Store/ResourceGraph admission 全部发生在 Accepted/首次 dispatch 前，失败不留下 Operation；
- Board terminal 只使 A candidate 有资格返回，不直接完成 Sweep；
- 项目原生 Sweep 在 B 与相关 Head/Operation/Event 同批提交后完成；
- C/Marker/Limit 可以独立随后求值；
- Query 再通过 Ticket/Reader 读取具体 A/B/Stage/C，不读取 worker 私有对象。

## 11. 错误与终态分类

所有 Module 使用同一顶层分类，但保留领域专用 detail variant：

| 分类 | 典型含义 | 是否创建 Operation | 重试方式 |
|---|---|---|---|
| `InvalidArgument` | 类型/范围/组合非法 | 否 | 修改请求 |
| `RevisionConflict` | expected revision 或 current-input token 过期 | 否或已有 Operation 失败 | 重新读取 Catalog 后决定 |
| `UnsupportedCapability` | Product/Board/Profile 不支持 | 否 | 更换能力/Profile，不盲重试 |
| `ResourceExhausted` | queue/pin/buffer/temp quota 不足 | 视入口而定 | 按 retry-after/资源终态重试 |
| `DeadlineExceeded` | 用户等待或工作 deadline 到期 | 可能进入 Drain | 等待 Drain/恢复，不立即复用资源 |
| `Cancelled` | stop/abort 被接受并最终终止 | 是 | 显式重新发起 |
| `PayloadExpired` | provenance 在但重算所需 payload 已回收 | 否 | 新 Sweep/重新导入 |
| `HardwareFaultSafe` | 失败但已证明 RF safe/off | 是 | 经授权 recovery |
| `HardwareFaultUnsafeRf` | 无法证明 RF 安全 | 是并锁存 | 物理隔离/kill/独立验证 |
| `ContractViolation` | Adapter 在 terminal 后回调、ID/generation 错等 | 是并 quarantine | 修复 Adapter；不能作为普通测量失败吞掉 |
| `InternalFailure` | 未分类 Implementation 故障 | 视入口而定 | 保留诊断并按安全影响处理 |

协议 Adapter 只把稳定 code 映射成 HTTP/SCPI 错误；不能反向从文本推断领域状态。SCPI Error Queue、ESR/STB 和 Web error body 读取同一个已提交结果，但按 Compatibility Profile 编码。

## 12. 线程、实时性与性能契约

| Interface | 允许阻塞 | 调用/回调约束 |
|---|---|---|
| L1 → Kernel submit/admit | 仅有硬上界的 admission/commit | 不等待 Sweep、文件、算法或硬件 |
| Kernel → Runtime reserve_work | 同步、有硬上界 | 容量不足在 Operation commit 前拒绝；不创建线程 |
| Kernel → Runtime dispatch | 仅有界所有权转交 | 已消费匹配 permit 后不再因普通队列容量拒绝；不创建线程 |
| Runtime → L4 run | worker 上可长运行 | 必须消费 ExecutionContext；不可中断工作转 Drain |
| Board prepare | 首选有界纯计算；SDK staging 只能在专用 Prepare worker | 可定向 abort；prepare terminal 是原 job return |
| Board start/abort | start/请求返回有硬上界 | Accepted 后终态走 Sink；回调不做重计算/I/O |
| Board safety | 独立 SafetyLane | 不与 acquisition/prepare/recovery worker 共用；readback 才是证据 |
| Store reserve/commit | 短暂有硬上界 | lifecycle terminal 与 Pending waiter pin 上界在初始 commit 前预留；I/O staging 在 commit 前；锁内不回调；预留终态失败只允许 Store integrity fail-stop |
| Query encoding | Binary Transfer lane 可长流式 | ReaderLease 覆盖全程；断线显式 finish |
| Blob upload | Binary Transfer lane 可长流式 | credit/size/quota 有界；大字节不进 Control Executor，断线显式 Abandoned；不可中断 cleanup 转 Drain |
| Preview | producer `try_publish` 不阻塞 | per-subscriber queue 可丢并报告 gap；不得反压正式 Acquisition |

目标是 AArch64 Linux PREEMPT 上的有界、可观察行为，不宣称用户态 C++ 获得硬实时。任何 P99/P99.9 时限和最大容量都必须由 ProductProfile、Capability 与目标机验证给出，本文不凭空填写数值。

## 13. Interface 版本和构建规则

- C++ Interface 与 Adapter 在同一目标中静态编译，不提供跨 MinGW/AArch64 或跨编译器动态 C++ 插件 ABI。
- `InterfaceContractVersion{major,minor}` 描述语义/schema；major 不兼容，minor 只能增加 capability-gated 的可选字段/variant。
- 未识别的 required variant 必须拒绝；不能忽略后继续执行。
- Capability/Manifest/Snapshot/Error 都保存 schema version 和 digest；不使用无类型扩展 map 绕过版本控制。
- MinGW Mock 与 AArch64 Real 分别编译同一 public headers 和 contract tests；通过 MinGW 不代表目标 SDK 已准入。
- Eigen 只属于 computation Implementation；HTTP 库只属于 Web Adapter；JSON codec 只属于协议/持久化 Adapter。

## 14. Interface 合同测试面

| Suite | 从哪个 Interface 测试 | 必须证明 |
|---|---|---|
| Protocol equivalence | `InstrumentKernel` | 等价 Web/SCPI 输入得到相同领域 revision、Operation、Snapshot 和错误语义 |
| Kernel behavior | `InstrumentKernel` + Mock Board/File | revision、权限、Command/Query、Operation、Event、last-good |
| Transfer/preview | Kernel upload/read/preview Interface | 跨 actor 拒绝、quota/credit、断线/TTL/Drain 清理、blob reader 统一 finish、Preview gap/terminal 且不影响正式结果 |
| Runtime model/pressure | `OperationRuntime` | admission 上限、公平、cancel、deadline、Drain、唯一 completion |
| Acquisition contract | `AcquisitionEngine` | 完整 ledger、失败不发部分 A、多板 all-terminal、Preview 隔离 |
| Board contract | 同一 Board suite 跑 Real/Mock/Replay | prepare/start/chunk/terminal/abort/safety/health/recovery 行为一致 |
| Algorithm golden | Measurement/Calibration Interface | 数值、质量传播和 provenance；不经 Diagram/协议绕测 |
| Sweep planning/admission | `SweepAdmissionPlanner` + Runtime/Store/ResourceArbiter | 同一 plan digest、硬上界、无副作用；所有 permit/lease 齐备后才 commit/dispatch，失败无幽灵 Operation |
| Store race/fault | `InstrumentStore` | 初始 commit 失败无 lifecycle/dispatch；publication commit 失败 abort candidate 后必以预留 capacity terminalize 或 fail-stop；Store spy 永不见 Runtime/Preview capability；普通 pin pool 已满时 `ContinuationJoinReservation` 仍完成 A join；多 waiter 各自 PendingResultPin reservation，单 caller quota/cancel 不回滚共享 publication；Runtime escrow 恰释放一次；Event feed replay-cut→live/stop/唯一 terminal；故障无半可见状态 |
| End-to-end tracer | L1 → L6 → L5 → L1 | Single Sweep、Marker、Limit、Diagram、Query/Event 使用同一正式事实 |

合同测试断言 Interface 可观察行为，不读取 Implementation 私有队列、线程或缓存。Mock 的测试控制使用独立 test-only seam，不能污染生产 `BoardExecutionPort`/Safety/Maintenance Interface。

## 15. 尚未冻结的证据

以下问题不会改变六层责任，但会补充具体 Capability、容量或 Adapter 实现：

1. 公司底软的回调线程、buffer 生命周期、最大 chunk、backpressure 和 abort terminal 证据；
2. `a/b` wave convention、Z0、单位、归一化、factory correction 和实际质量位；
3. RF-off/safe readback、SafetyLane 与物理 kill/interlock 是否真实独立；
4. AArch64 SDK 的 C++17、atomic/thread/filesystem/socket、异常/RTTI和工具链选项；
5. 各 Profile 最大 points/ports/traces/markers/limits、pin bytes、worker/queue 和 deadline；
6. cpp-httplib、Eigen 和候选 JSON/TLS 库的双工具链准入结果。

这些项必须进入版本化 Capability、平台准入报告或 HIL 证据；不得通过给 Mock 增加“理想能力”把未知事实伪装成已支持。
