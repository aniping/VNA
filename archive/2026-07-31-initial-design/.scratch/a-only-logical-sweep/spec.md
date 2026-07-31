# 单板 A-only Logical Sweep 端到端纵切

Status: done

## Problem Statement

当前代码已经分别验证了 BoardPort 的 `prepare → run` 异步契约、MockBoard 的确定性接收机波量交付、固定容量 OperationRuntime、InstrumentStore 的操作生命周期预留，以及 SweepAdmissionController 的“先提交 Accepted，再派发”顺序。但是这些能力仍是彼此分离的合同切片：现有跨层测试使用同步完成的通用 RuntimeWork，没有经过 MockBoard；InstrumentStore 也只保存 OperationSnapshot，不能发布正式 A 层数据。

这意味着系统目前无法证明一次扫描能够从 L2 准入开始，经过 L3 运行时、L4 Acquisition、L6 Board Adapter，最终在 L5 原子形成完整扫频快照。同步 `RuntimeWork::execute()` 返回终态的模型也无法正确承载 BoardPort Accepted 后才发生的异步 Prepare、Run、chunk 和 terminal 回调。在这条数据链闭合前继续实现 B 层测量、校准、Trace、Web 或 SCPI，会迫使上层依赖临时数组或绕过正式所有权边界。

本规格解决首条真实采集数据纵切，但只面向开发环境中的单板 Mock。它必须保持生产架构的不变量，同时明确不具备真实 RF 安全准入，不能连接 Real Board Adapter。

## Solution

实现一个显式授权的单板 A-only Logical Sweep：L2 从同一个冻结输入生成有界计划，在任何可见 Operation 和首次 dispatch 前取得 Runtime、Store、采集 Buffer、Ingress 与 Board callback 所需容量；初始事实提交成功后，L3 以非阻塞方式启动 L4 AcquisitionEngine。AcquisitionEngine 通过 BoardPort 完成 Prepare、实际 Manifest 校验、既有资源 envelope 内的精确收窄和 Run，随后把全部必需 `a/b` Receiver Wave Quantity 交给有界 Network Observation Builder。

只有 Manifest 派生的 required observation map、实际轴、chunk sequence、point coverage、质量和唯一成功 terminal 全部闭合后，Builder 才产生不可见的 A `CompletedSweepBundle` candidate。L4 通过 L3 的可靠 completion，把 candidate、与冻结 purpose 匹配的 `AuthorizedAOnlyCompletionOwner` 及显式 disabled Preview finalization owner 一并交回 L2。L2 再使用 InstrumentStore 的单一 DomainCommitBundle 边界，使不可变 A、A-only Acquisition Operation 终态、fence/status 与完成 Event 在同一 revision 原子可见，并在 commit receipt 后恰好一次终结留在 Store 外的 A-only/Preview owner。

这里的“A-only”表示本次明确授权的 raw/diagnostic purpose 只发布大写数据阶段 A，不继续派发 B 层 Measurement Pipeline；它不表示只采集小写入射波 `a`。实际需要的入射波、响应波、source state 和 receiver path 由冻结计划及 Prepared Execution Manifest 共同决定。普通 Channel Sweep 仍必须在 B 层 Completed Measurement Snapshot 发布后完成，不能借 A-only 绕过测量处理门禁。

## User Stories

1. As a VNA core developer, I want to submit one typed A-only acquisition request at L2, so that I can exercise the complete acquisition architecture without using Web or SCPI.
2. As a VNA core developer, I want submission to return an Operation identity rather than measurement arrays, so that accepted work remains distinct from completed data.
3. As an instrument architect, I want A-only execution to require an explicit raw/diagnostic authorization, so that normal Channel sweeps cannot silently bypass B-layer processing.
4. As an instrument architect, I want A-only to mean “publish only data stage A,” so that it is never confused with collecting only the incident receiver wave `a`.
5. As an RTOS integrator, I want all critical capacity reserved before hardware work starts, so that acquisition never depends on unbounded allocation after admission.
6. As an RTOS integrator, I want Board Prepare and Run handled without blocking a task waiting for callbacks, so that the design can later map to bounded RTOS tasks and mailboxes.
7. As an RTOS integrator, I want completion delivery pre-reserved and non-droppable, so that an accepted operation cannot remain permanently pending when progress traffic is congested.
8. As an application developer, I want initial admission failure to create no visible Operation, so that clients never observe a ghost operation that was never dispatched.
9. As an application developer, I want the Operation to become visible before Runtime dispatch, so that every accepted operation already owns enough capacity to record a terminal state.
10. As an application developer, I want a post-commit internal dispatch violation to terminate the visible Operation as Failed, so that the system does not pretend the operation was never accepted.
11. As a board adapter implementer, I want Prepare to receive a frozen SweepIntent and matching authorization, so that stale capability, topology or operational revisions are rejected before Run.
12. As a board adapter implementer, I want Prepare to return an actual immutable Manifest, so that quantized point count, frequency axis and required observations become execution facts rather than assumptions.
13. As an acquisition developer, I want Manifest finalization to consume only pre-admitted capacity, so that L4 cannot allocate new critical resources, enlarge the plan or switch boards after Prepare.
14. As a board adapter implementer, I want synchronous rejection to return every move-only input, so that authorizations, tokens, delivery grants and callback registrations never leak.
15. As a board adapter implementer, I want Accepted requests to callback only after submission returns and to emit exactly one terminal, so that callers cannot be re-entered before saving the accepted identity.
16. As an acquisition developer, I want Board callbacks to deposit bounded events instead of calling L2 or InstrumentStore, so that adapter callback context cannot reverse-enter the domain core.
17. As an acquisition developer, I want every formal chunk delivered through a move-only AcquisitionChunkLease, so that the adapter and Builder cannot concurrently own the same data.
18. As a real-board integrator, I want the contract to support copying once at the callback boundary when the driver reuses its buffer, so that later Real adapters do not depend on an unproven zero-copy ABI.
19. As an acquisition developer, I want observations keyed by typed source-state, receiver-path and wave identity, so that future port counts are not encoded as fixed array positions or board-model conditionals.
20. As an acquisition developer, I want the Builder to validate Manifest identity, Run generation, chunk sequence and point coverage, so that delayed or unrelated callbacks cannot contaminate the current sweep.
21. As an acquisition developer, I want re-orderable non-overlapping chunks assembled by their declared identity and coverage, so that transport order alone does not define the completed result.
22. As an acquisition developer, I want conflicting duplicates, overlaps, gaps and out-of-range chunks rejected, so that partial or ambiguous data is never padded into a formal snapshot.
23. As a measurement developer, I want all required incident and response observations present before A publication, so that later ratio and network processing receives a complete formal input.
24. As a measurement developer, I want actual axis and Manifest provenance stored with A, so that processing never substitutes requested frequencies for the values the board actually executed.
25. As a quality engineer, I want receiver validity, quality flags and their real granularity stored alongside complex values, so that overload and lock failures are not reduced to log messages.
26. As a quality engineer, I want terminal success insufficient by itself to publish A, so that the system must also prove the complete observation ledger is closed.
27. As a quality engineer, I want failure or cancellation to produce no partial A, so that downstream processing cannot consume zero-filled or stale mixed-generation data.
28. As a data consumer, I want CompletedSweepBundle to be immutable after publication, so that concurrent readers observe a stable axis, values, quality and provenance closure.
29. As a data consumer, I want each A bound to a LogicalSweepId and a bounded BoardRunEvidence collection, so that the same schema can later represent a validated multi-board result without changing single-board semantics.
30. As a data consumer, I want the single-board evidence to include Manifest, session/capability revision, BoardRunId, generation and completion ledger, so that a result can be audited without reading Mock private state.
31. As an InstrumentStore maintainer, I want a candidate to remain invisible until DomainCommitBundle succeeds, so that workers cannot publish data directly.
32. As an InstrumentStore maintainer, I want A, the A-only operation terminal, status/fence and completion Event committed in one revision, so that control, formal data and notification cannot disagree.
33. As an InstrumentStore maintainer, I want a failed final commit to leave A and completion Event invisible, so that a partially applied publication never escapes.
34. As an InstrumentStore maintainer, I want a failed final commit to use the pre-installed terminal reservation for a state-only Failed commit, so that ordinary capacity pressure cannot strand a visible operation.
35. As a future Measurement Pipeline developer, I want A-only completion to create no fake B, no empty continuation handoff and no successor dispatch, so that A-only and normal Sweep completion retain different meanings.
36. As a future Measurement Pipeline developer, I want the A schema to be suitable as a canonical processing input, so that the next milestone can build B without revisiting Board callback ownership.
37. As a MockBoard test author, I want deterministic virtual time and configurable chunk plans, so that success, rejection, delay, reordering, missing data and protocol violations are reproducible without sleeps.
38. As a MockBoard test author, I want the Mock run output derived from its accepted Prepared Manifest, so that a separately configured point count cannot be mistaken for a valid completed sweep.
39. As a test author, I want one high-level seam from L2 submission to L5 observation, so that tests verify external behavior instead of private queues and state-machine methods.
40. As a test author, I want the Mock control surface used only to advance virtual time or inject an advertised scenario, so that tests cannot manually call Prepare and Run to manufacture a false end-to-end result.
41. As a maintainer, I want fixed upper bounds for operations, observations, points, chunks, events and buffers, so that resource exhaustion is explicit and testable.
42. As a maintainer, I want typed errors to preserve failure phase, identities and retry/safety classification, so that later Web and SCPI adapters can map the same facts consistently.
43. As a maintainer, I want every new public contract documented with parameters, results, ownership and callback timing, so that future board adapters can implement it without reverse-engineering tests.
44. As a product owner, I want Real Board Adapter activation explicitly blocked by missing SafetyLane, abort/readback and HIL evidence, so that a Mock acquisition milestone is not misrepresented as production RF readiness.
45. As a future protocol developer, I want operation and snapshot facts independent of transport sessions, so that Web and SCPI can later observe the same authority rather than maintaining separate current arrays.
46. As an ownership-safety reviewer, I want successful acquisition to return the matching AuthorizedAOnlyCompletionOwner and disabled PreviewFinalizationOwnerSet with its candidate, so that no pre-admitted capability disappears between L4, L3 and L2.
47. As an ownership-safety reviewer, I want ExecutionContext and a minimal AcquisitionDraining handoff retained in this milestone, so that accepted asynchronous obligations are never abandoned merely because full user cancellation policy is deferred.
48. As a resource-arbitration developer, I want ExactFinalizationCapability signed before first dispatch and consumed locally after Manifest, so that L4 cannot race another admission or call back into the arbiter while holding Board resources.
49. As a fault-diagnostics developer, I want callbacks with the wrong generation or after terminal to latch a typed ContractViolation and isolate the faulty Mock session, so that protocol corruption is not reduced to “no snapshot was published.”

## Implementation Decisions

### Scope and terminology

- This milestone implements one explicitly authorized, single-board, A-only raw/diagnostic acquisition purpose using MockBoardProvider.
- A-only publishes A `CompletedSweepBundle` and does not dispatch Measurement Pipeline work. It still collects every `a/b` Receiver Wave Quantity required by the frozen plan and actual Manifest.
- The visible lifecycle must be named as an A-only acquisition operation or carry an explicit A-only completion policy. It must not redefine the native product SweepOperation, whose normal completion fence remains B-layer publication.
- The result is a development tracer bullet, not a complete VNA feature and not a production RF execution path.

### Layer responsibilities and command flow

- The highest entry is an L2 InstrumentKernel/application seam. The caller supplies a typed A-only request, not a RuntimeWork implementation, Board token or output buffer.
- L2 reads or constructs one frozen, authorized input cut and uses a pure SweepAdmissionPlanner to produce a FrozenSweepJob, SweepIntent, required observation plan, conservative capacity claim and a shared plan digest.
- The initial vertical slice may use a minimal typed catalog/profile model, but it must not replace revisions and typed facts with arbitrary key/value maps.
- L2 performs all admission before the first dispatch: Runtime work/completion reservation, Store output and lifecycle-terminal reservation, acquisition Buffer/Ingress capacity, Board prepare/run call and sink capacity, and single-board resource pre-admission.
- A-only admission owns an AuthorizedAOnlyCompletionOwner. It does not reserve a required post-acquisition worker and does not manufacture an empty continuation join.
- Single-board resource pre-admission signs an ExactFinalizationCapability before first dispatch. It is bound to the same topology epoch, conservative envelope and plan digest as the FrozenSweepJob.
- Preview production is disabled in this milestone, but admission still creates an explicit disabled PreviewFinalizationOwnerSet bound to Operation/generation. The empty/disabled state follows the same success, failure and Draining ownership shape as an enabled preview and never enters InstrumentStore.
- Only after the initial DomainCommitBundle makes the Operation Accepted and installs its terminal reservation may L2 dispatch the FrozenSweepJob.
- Initial commit failure releases the complete local admission owner, creates no Operation or Event, and never invokes BoardPort.

### Asynchronous OperationRuntime

- The current synchronous one-shot RuntimeWork contract is insufficient for acquisition and must not block while waiting for Board callbacks.
- Runtime dispatch consumes a typed frozen acquisition work item, its pre-reserved permit set and a completion registration bound to WorkId/generation.
- Starting the work and completing the work are separate events. After start, the Runtime slot remains Running until the work produces one typed RuntimeWorkCompletion or transfers all outstanding ownership to a named Drain.
- Runtime supplies an ExecutionContext carrying stop, deadline, budget and progress capabilities even though this milestone exposes no user-facing cancellation command. Acquisition checks it at bounded state transitions.
- Board callbacks may only validate bounded metadata, move leases into pre-reserved ingress and enqueue phase/chunk/terminal events. They do not call InstrumentKernel or InstrumentStore and do not execute network, file or heavy numerical processing.
- Completion delivery is reliable and pre-reserved. Progress may be bounded or coalesced, but completion cannot be dropped.
- Runtime retains a deterministic pump surface for tests; pumping starts/resumes queued work and drains completion mailboxes without using wall-clock sleeps.
- Existing synchronous test work may be adapted as an immediately completing asynchronous work item; acquisition must not be forced back into the synchronous execute-return model.

### AcquisitionEngine and state model

- L4 AcquisitionEngine owns the prepared execution, exact run resources, Board callback ingress, private observation ledger, candidate construction and cleanup until a typed terminal returns through L3.
- Its observable phases cover queued/start, preparing, Manifest finalization, acquiring, finalizing and a single succeeded/failed/draining outcome. Accepted Board submissions are not terminal states.
- Prepare uses the frozen SweepIntent and matching revision/digest authorization. The returned Prepared Execution Manifest is the authority for actual point count, actual axis and required observation map.
- Manifest finalization consumes the pre-signed ExactFinalizationCapability entirely inside L4 and only narrows the already admitted conservative envelope. It cannot call L2, InstrumentStore, Runtime or ResourceArbiter, obtain new capacity, switch boards or enlarge the observation plan. Any mismatch, expansion or stale revision fails before Run and releases or drains the prepared ownership through the Board contract.
- Run begins only with a valid PreparedStartToken, StartAuthorization, RunDeliveryGrant and pre-reserved BoardRunSink registration.
- AcquisitionEngine never writes L5, advances a Head or sends a completion Event. Success returns a sealed PublicationCandidateBatch together with the matching `AuthorizedAOnlyCompletionOwner` and disabled PreviewFinalizationOwnerSet; failure returns no candidate plus resource-terminal evidence and the Preview owner; Draining transfers the acquisition resources, A-only completion owner and Preview owner together to one typed AcquisitionDrainOwner.
- The minimal Draining path is part of this milestone: once an accepted Board obligation cannot reach a true resource terminal within the bounded ExecutionContext transition, Runtime retains the lane and complete AcquisitionDrainOwner under a DrainId. Full stop policy, physical RF proof and recovery UI remain deferred.

### Receiver observations and Builder

- ReceiverObservationChunk identity is sufficient to distinguish Manifest, prepared execution, Board run, generation, source state, receiver path, wave quantity, sequence and point range.
- The first Mock scenario may advertise one incident and one response observation, but Builder logic is driven by a bounded Manifest observation map rather than hard-coded A/B array positions.
- Chunk payload ownership moves exactly once into Acquisition Ingress. If an adapter cannot transfer driver-buffer lifetime, it copies once into an already reserved project BufferPool before returning from the callback.
- Network Observation Builder is the only long-lived chunk owner. It assembles by typed observation identity and point coverage, not callback arrival order.
- The ledger detects missing ranges, out-of-range points, conflicting duplicates, overlaps, wrong IDs/generation, terminal-before-complete, callback-after-terminal and multiple terminals.
- A successful Board terminal is necessary but not sufficient. Every required observation and point range must be present and compatible with the Manifest before sealing.
- Formal chunks are never silently dropped. Unexpected Ingress or BufferPool exhaustion fails the run and follows the explicit cleanup path.
- Quality travels with the corresponding values at its real granularity. The vertical slice preserves Mock quality flags and leaves later quality transforms to B/Stage/C processing.

### CompletedSweepBundle and provenance

- A is an immutable CompletedSweepBundle with a typed snapshot identity, LogicalSweepId, operation relation, actual excitation axis, immutable complex observation buffers, QualityPlane, plan/configuration revisions and bounded provenance.
- Its BoardRunEvidence collection retains an array shape with exactly one element in this milestone. The element records the parent Manifest identity/digest, Board session and capability facts, BoardRunId, run generation, expected/received coverage, sequence ledger and unique terminal evidence.
- A candidate is not queryable and cannot be used by another worker before commit. CandidateCommitLease owns its buffers and parent closure until commit or abort.
- Published A is never modified in place. This milestone does not introduce a mutable “current A array” or advance ChannelMeasurementHead.
- Previously published snapshots remain unchanged when a later acquisition fails. Retention beyond the operation-linked development query is deferred, but no new implementation may overwrite an existing immutable A.

### InstrumentStore and atomic visibility

- InstrumentStore remains the single public L5 transaction boundary. Internal snapshot storage, operation catalog, event journal and commit coordination are not exposed as separate write APIs.
- The minimal typed DomainCommitBundle supports the facts required by this vertical slice without becoming an arbitrary property dictionary.
- The initial bundle makes the A-only Operation Accepted, installs its LifecycleTerminalReservation and records the WorkId/plan digest as non-executable correlation facts.
- The success bundle atomically publishes the A candidate, marks the A-only operation Completed, satisfies its fence/status and appends a typed completion Event in one Store revision.
- Fence/status/Event records are mandatory fields of the Store transaction required by ADR-0008; they do not add an EventFeed, Web or SCPI subscription surface in this milestone.
- During the synchronous Store commit, L2 continues to own the AuthorizedAOnlyCompletionOwner and disabled PreviewFinalizationOwnerSet. Commit success retires/finalizes each exactly once against the published A typed reference; commit failure retains them until the state-only failure fact is visible and then finalizes them as failed/discarded.
- A-only success carries no ContinuationStoreJoinRequest, no Runtime escrow and no fabricated successor handoff. The A-only owner is consumed locally by L2 rather than converted into an empty Store handoff.
- A final commit failure consumes or aborts the candidate owner, leaves A and the completion Event invisible, and preserves the installed lifecycle terminal reservation.
- L2 then performs a bounded state-only failure commit so the existing operation, status/fence and failure Event become consistent. Only a Store integrity failure may put the instrument into fail-stop.
- No Runtime permit, RuntimeCompletionRegistration, Preview owner, Board token or mutable Builder state may enter InstrumentStore.
- The sole business entry in acceptance tests is the public L2 A-only submit surface. Result observation uses the existing public read-only InstrumentStore fact surface directly; this specification does not add a second L2 query facade. The Store returns a copied snapshot view or opaque read handle whose lifetime covers all referenced buffers, never raw internal Buffer pointers.

### MockBoard behavior

- MockBoard remains an implementation of the same BoardPort used by future Real and Replay adapters; no Mock-specific branch enters L2/L4/L5 domain behavior.
- Virtual time controls when accepted Prepare and Run work emits callbacks. Accepted calls remain non-inline and each emits one terminal.
- Mock scenarios describe bounded chunk plans, including multiple chunks, reordered delivery, missing coverage, conflicting duplicate, wrong generation, terminal failure and callback-after-terminal fault injection.
- Run output is derived from the accepted Prepared Execution Manifest. Scenario data that cannot satisfy the Manifest is rejected or produces an explicit failed run; it cannot publish a mismatched A.
- Mock observation counters remain diagnostic support for Board contract tests, but the new end-to-end acceptance tests do not use them as proof of formal publication.
- A driver-buffer-reuse scenario overwrites its source storage immediately after the callback boundary to prove the lease/copy contract.
- Deliberate wrong-generation or post-terminal fault injection is treated as a non-conforming adapter scenario. Acquisition records a stable ContractViolation, latches the Mock session into an isolated fault state for subsequent execution, and publishes no A.

### Error, ownership and production gate

- Cross-seam failures use project-owned typed errors and terminals; exceptions, JSON, Eigen, socket, filesystem and vendor SDK types do not cross public interfaces.
- Error facts identify phase and relevant typed identities and leave room for retry and safety classification. Callers do not parse diagnostic text for behavior.
- RAII may release unused in-process reservations, but it is not used as an abort, RF-off, operation-terminal or Drain command.
- A minimal ExecutionContext and typed AcquisitionDraining owner are required even in the Mock-only path. They prove capability conservation; they do not claim that physical RF is safe.
- This milestone executes only MockBoard and must be impossible to configure with a Real Board Adapter. Production RF enablement remains blocked until BoardSafetyLane, abort, RF-off/readback, Drain/Quarantine, bottom-software ABI and HIL acceptance are implemented and approved.

### Build and source conventions

- Implementation uses C++17 and the existing MinGW CMake build. GoogleTest is consumed from the repository-local pinned archive only when BUILD_TESTING is enabled.
- No new third-party dependency is introduced. Eigen3 and cpp-httplib are not needed for this milestone.
- Runtime-facing containers, mailboxes, buffers and ledgers have explicit Product/Profile or contract limits. Critical resources are not obtained after hardware start.
- New classes follow the project convention of one primary class per `.h`/`.cpp` pair where behavior is non-trivial. Modules compose through their existing CMake subdirectories and targets.
- Every new or modified public API includes Doxygen documentation for purpose, parameters, results, ownership, lifetime, rejection and callback timing.

## Testing Decisions

- The only new end-to-end acceptance seam starts at the public L2 A-only submission surface, uses MockBoardControl solely to load a scenario and advance virtual time, and observes Operation plus formal Snapshot facts through the public read-only InstrumentStore fact surface. Tests do not submit work through L5 and do not introduce an L2 query facade.
- Acceptance tests never call BoardExecutionPort Prepare or Run directly, never pass an arbitrary RuntimeWork into the submission surface, and never inspect Runtime slots, AcquisitionEngine state, Builder arrays or Store private catalogs.
- A good test asserts observable lifecycle, immutable result content, provenance, quality and atomic visibility. It does not assert private method order when the same public contract could be implemented differently.
- Existing BoardPort, OperationRuntime and InstrumentStore contract tests remain useful lower-level compatibility gates. The new feature is considered complete only when the high-level seam passes.
- All asynchronous tests use virtual time and deterministic runtime pumping. No test uses wall-clock sleep, background timing assumptions or real sockets.
- Happy-path acceptance submits one authorized A-only Logical Sweep, observes Accepted with no A, advances Mock Prepare and Run, then observes exactly one immutable CompletedSweepBundle and a Completed operation/event in the same Store revision.
- Happy-path content checks include actual axis, every required incident/response observation, exact complex values, quality flags, Manifest identity/digest, BoardRunId, generation and complete coverage ledger. No content-hash algorithm is assumed by this milestone.
- The end-to-end test also asserts that no B, Stage or C publication exists. Capability conservation, absence of successor dispatch and absence of an empty continuation handoff are verified separately at the existing Runtime/Acquisition ownership contract seam because they are not externally observable facts.
- Admission-capacity, invalid revision and initial Store-commit failures synchronously reject with no visible Operation, no Board callback and no completion Event.
- Prepare rejection and Manifest finalization failure produce no Run, no A and one Failed operation after real cleanup terminal semantics are satisfied.
- Run rejection returns all move-only inputs, produces no callbacks and results in no A and a Failed operation.
- Missing chunk, conflicting duplicate, overlap, out-of-range coverage and failed terminal scenarios publish no A and leave prior immutable facts unchanged. Wrong Manifest/Run/generation, multiple terminal and callback-after-terminal scenarios additionally expose a stable ContractViolation and isolate the faulty Mock session.
- Reordered but non-overlapping chunks with complete declared coverage assemble successfully, proving that callback order is not the data model.
- Formal Ingress/BufferPool exhaustion never drops a chunk and publishes a partial A; it produces a typed failure and releases or transfers every owner exactly once.
- Driver-buffer-reuse acceptance overwrites adapter source memory immediately after callback return and verifies every published complex value and quality entry remains correct.
- InstrumentStore contract fault injection proves A and completion Event are both invisible, then proves the pre-installed terminal reservation permits a state-only Failed commit without leaving Pending/Publishing. Event atomicity is tested at this Store contract seam; the end-to-end seam does not add EventFeed transport.
- Runtime tests prove dispatch is non-inline, the slot stays Running across Board callback delays, completion is delivered exactly once, and the slot is not released before true terminal or typed Drain handoff.
- Runtime/Acquisition ownership contract tests prove `AcquisitionSucceeded` returns candidate + matching A-only completion owner + disabled Preview owner, L2 retires them exactly once after commit, no successor is dispatched, and no empty Store handoff is created.
- A minimal Draining contract test proves an accepted non-terminal Mock obligation transfers the complete AcquisitionDrainOwner and keeps Runtime capacity unavailable until Drain terminal; it does not claim physical RF-off.
- A second successful A-only acquisition creates a distinct immutable snapshot rather than mutating the first. A later failure leaves both prior snapshots unchanged.
- The MinGW debug preset must configure from the local GoogleTest archive, build all targets and pass the full test suite. A separate BUILD_TESTING=OFF build must compile runtime and Mock product targets without extracting, building or linking GoogleTest.

## Out of Scope

- Normal Channel Sweep completion at B, receiver ratios, S-parameters, averaging, user calibration and CompletedMeasurementBundle.
- Measurement Stage materialization, Analysis Trace evaluation, Marker, Limit, Diagram and C publication.
- Web HTTP endpoints, browser UI, SCPI parser/socket server, compatibility command mapping and client-session semantics.
- Sweep Preview production, PreviewTile generation, PreviewHub transport, browser throttling and Preview gap/resnapshot behavior. A typed disabled PreviewFinalizationOwnerSet is still carried to preserve the required ownership shape, but no Preview data feature is delivered here.
- Continuous, Groups, average sequences, segmented sweep, CW/CW-Time, trigger orchestration, power sweep and time-domain transforms.
- Multi-board execution, CompositeSweepCoordinator and coherence/timebase/skew validation. The A provenance collection remains bounded and array-shaped, but its count is one.
- Real or Replay Board Adapter, company bottom-software integration, AArch64 RTOS toolchain validation, hardware-in-the-loop tests and production RF safety certification.
- BoardSafetyLane, emergency interlock/kill, user-facing cancellation/deadline policy, physical RF timeout proof, quarantine recovery and the complete Drain supervisor workflow. The minimal ExecutionContext checks and typed AcquisitionDraining ownership handoff are in scope; consequently the milestone remains Mock-only and cannot activate physical RF.
- Calibration Session, Calibration Observation, solver, Correction Set, correction binding and verification.
- QueryTicket/ResultPin production protocol, full retention/tombstone policy, persistence, State Save/Recall, Touchstone/CSV/report export and Blob transfer.
- Full EventFeed replay/live delivery to Web or SCPI. The atomic Store record required by this slice is included, but transport subscriptions are deferred.
- Zero-copy driver integration, and any zero-copy guarantee made before the bottom-software buffer lifetime and callback-thread ABI are documented and verified.
- Numerical performance, WCET and long-duration soak certification on the target RTOS. This specification requires bounded design and deterministic desktop tests but does not certify target timing.

## Further Notes

- The current codebase has separate, passing Board, Runtime, Store and cross-layer contract tests, but no AcquisitionEngine, CompletedSweepBundle, A Catalog, DomainCommitBundle or end-to-end Mock scan publication. The existing cross-layer test must not be described as a completed scan.
- ReceiverObservationChunk is a temporary Board delivery object, not a Completed Sweep Snapshot.
- The current Mock scenario is effectively single-chunk and has a point count independent from the Prepare Manifest. This milestone must close that mismatch rather than treating a successful Mock terminal as proof of complete A.
- Existing architecture decisions remain authoritative: Board Adapter is isolated from application semantics; Preview is not formal data; publications are immutable; control, formal data and notification are separate flows with one atomic Store visibility boundary.
- Unknown bottom-software facts—buffer transferability, callback thread/context, abort behavior, RF-off/readback, receiver-path identity and multi-board coherence—remain explicit external gates. The Mock-only slice must not silently invent production guarantees for them.
- After this specification is implemented and accepted, the next independent specification should consume A through a Measurement Pipeline and publish B CompletedMeasurementBundle for a normal SweepOperation. Web/SCPI and Analysis/Display work should still call the same InstrumentKernel and Store authority rather than adding transport-owned arrays.
