#include "runtime/function/acquisition/acquisition_engine.h"

#include <limits>
#include <utility>
#include <variant>

namespace {

vna::acquisition::AcquisitionRetryClass retry_for_board_error(
    vna::board::BoardErrc error) noexcept {
    using vna::acquisition::AcquisitionRetryClass;
    using vna::board::BoardErrc;
    switch (error) {
        case BoardErrc::StaleSessionEpoch:
        case BoardErrc::StaleCapability:
        case BoardErrc::StaleTopologyEpoch:
        case BoardErrc::StaleOperationalEpoch:
            return AcquisitionRetryClass::AfterStateRefresh;
        case BoardErrc::Busy:
        case BoardErrc::ResourceExhausted:
            return AcquisitionRetryClass::AfterResourceRelease;
        case BoardErrc::ContractViolation:
        case BoardErrc::Closed:
            return AcquisitionRetryClass::AfterRecovery;
        case BoardErrc::InvalidIntent:
        case BoardErrc::Unsupported:
        case BoardErrc::AuthorizationMismatch:
            return AcquisitionRetryClass::DoNotRetryWithoutChange;
    }
    return AcquisitionRetryClass::AfterRecovery;
}

vna::acquisition::AcquisitionRetryClass retry_for_failure(
    vna::acquisition::AcquisitionFailureReason reason) noexcept {
    using vna::acquisition::AcquisitionFailureReason;
    using vna::acquisition::AcquisitionRetryClass;
    switch (reason) {
        case AcquisitionFailureReason::StopRequested:
            return AcquisitionRetryClass::ExplicitResubmission;
        case AcquisitionFailureReason::DeadlineExpired:
        case AcquisitionFailureReason::BudgetExhausted:
            return AcquisitionRetryClass::AfterResourceRelease;
        case AcquisitionFailureReason::ManifestOutsideAdmission:
            return AcquisitionRetryClass::AfterStateRefresh;
        case AcquisitionFailureReason::InvalidAdmissionResources:
        case AcquisitionFailureReason::BoardRejected:
        case AcquisitionFailureReason::BoardPrepareFailed:
        case AcquisitionFailureReason::BoardTerminalFailed:
        case AcquisitionFailureReason::BoardPrepareDraining:
        case AcquisitionFailureReason::BoardContractViolation:
        case AcquisitionFailureReason::IncompleteObservationSet:
        case AcquisitionFailureReason::StoreCommitRejected:
        case AcquisitionFailureReason::RuntimeDispatchContractViolation:
            return AcquisitionRetryClass::AfterRecovery;
    }
    return AcquisitionRetryClass::AfterRecovery;
}

vna::acquisition::AcquisitionSafetyImpact safety_for_failure(
    vna::acquisition::AcquisitionFailurePhase phase,
    vna::acquisition::AcquisitionFailureReason reason) noexcept {
    using vna::acquisition::AcquisitionFailurePhase;
    using vna::acquisition::AcquisitionFailureReason;
    using vna::acquisition::AcquisitionSafetyImpact;
    if (reason == AcquisitionFailureReason::BoardPrepareDraining) {
        return AcquisitionSafetyImpact::ResourceIsolationRequired;
    }
    if (phase == AcquisitionFailurePhase::Run &&
        reason == AcquisitionFailureReason::BoardRejected) {
        return AcquisitionSafetyImpact::NoRunAccepted;
    }
    if (phase == AcquisitionFailurePhase::Run ||
        phase == AcquisitionFailurePhase::CandidateSealing ||
        phase == AcquisitionFailurePhase::PublicationCommit) {
        return AcquisitionSafetyImpact::RunTerminalObserved;
    }
    return AcquisitionSafetyImpact::NoRunAccepted;
}

void attach_manifest_context(
    vna::acquisition::AcquisitionFailure& failure,
    vna::board::ManifestId manifest,
    vna::board::BoardSessionId session,
    std::uint64_t capability_revision) noexcept {
    failure.manifest = manifest;
    failure.board_session = session;
    failure.capability_revision = capability_revision;
}

}  // namespace

namespace vna::acquisition {

AcquisitionEngine::AcquisitionEngine(
    board::BoardExecutionPort& execution,
    board::SweepIntent intent,
    board::PrepareAuthorization&& prepare_authorization,
    board::PrepareCallId prepare_call,
    board::BoardRunId run,
    board::RunGeneration generation,
    CompletedSweepId snapshot_id,
    LogicalSweepId logical_sweep_id,
    runtime::WorkId work,
    std::size_t ingress_capacity,
    board::AcquisitionContinuationAttestation continuation,
    board::RunDeliveryGrant&& delivery,
    runtime::DrainId drain,
    AcquisitionAdmissionPool::Lease&& resources,
    board::BoardExecutionReservation&& board_reservation) noexcept
    : execution_(&execution),
      intent_(intent),
      prepare_authorization_(std::move(prepare_authorization)),
      prepare_call_(prepare_call),
      run_(run),
      generation_(generation),
      snapshot_id_(snapshot_id),
      logical_sweep_id_(logical_sweep_id),
      work_(work),
      ingress_(ingress_capacity),
      continuation_(continuation),
      delivery_(std::move(delivery)),
      drain_(drain),
      resources_(std::move(resources)),
      board_reservation_(std::move(board_reservation)) {}

runtime::RuntimeWorkStep AcquisitionEngine::start(
    runtime::ExecutionContext& context) noexcept {
    if (context.stop().stop_requested()) {
        return fail(
            AcquisitionFailurePhase::Admission,
            AcquisitionFailureReason::StopRequested);
    }
    if (context.deadline().expired()) {
        return fail(
            AcquisitionFailurePhase::Admission,
            AcquisitionFailureReason::DeadlineExpired);
    }
    if (!consume_transition_budget(context)) {
        return fail(
            AcquisitionFailurePhase::Admission,
            AcquisitionFailureReason::BudgetExhausted);
    }
    if (!resources_.owns_pre_dispatch_resources() ||
        !board_reservation_.valid() ||
        execution_ == nullptr || !prepare_authorization_.valid() ||
        !prepare_call_.valid() || !run_.valid() || !generation_.valid() ||
        !snapshot_id_.valid() || !logical_sweep_id_.valid() || !work_.valid() ||
        !ingress_.valid() ||
        !continuation_.valid() || !delivery_.valid()) {
        return fail(
            AcquisitionFailurePhase::Admission,
            AcquisitionFailureReason::InvalidAdmissionResources);
    }

    auto submission = execution_->begin_prepare(
        board_reservation_,
        prepare_call_,
        intent_,
        std::move(prepare_authorization_),
        board::PrepareSinkRegistration{*this});
    if (auto* rejected = std::get_if<board::PrepareRejected>(&submission)) {
        return fail_board_rejection(
            AcquisitionFailurePhase::Prepare, rejected->error);
    }
    const auto accepted = std::get<board::PrepareAccepted>(submission);
    if (accepted.call != prepare_call_) {
        return drain_contract_violation(
            AcquisitionFailurePhase::Prepare,
            DrainObligation::PrepareTerminal);
    }
    phase_ = Phase::Preparing;
    return runtime::RuntimeWorkStep::running();
}

runtime::RuntimeWorkStep AcquisitionEngine::resume(
    runtime::ExecutionContext& context) noexcept {
    if (phase_ == Phase::DiscardingPrepared) {
        if (!discard_terminal_.has_value()) {
            // Prepared cleanup 使用 Prepare admission 时预留的独立 terminal route；
            // 用户 stop/deadline 不能撤销这项板侧 cleanup 义务。
            return runtime::RuntimeWorkStep::running();
        }
        const auto terminal = *discard_terminal_;
        discard_terminal_.reset();
        const bool cleanup_closed =
            !discard_contract_violation_ && terminal.prepared == prepared_ &&
            terminal.kind == board::DiscardPreparedTerminalKind::Discarded;
        if (!cleanup_closed) {
            failure_.reason = AcquisitionFailureReason::BoardContractViolation;
            failure_.has_board_error =
                terminal.kind ==
                board::DiscardPreparedTerminalKind::CleanupFailed;
            failure_.board_error = terminal.error.code;
            failure_.retry = AcquisitionRetryClass::AfterRecovery;
            failure_.safety =
                AcquisitionSafetyImpact::ResourceIsolationRequired;
            phase_ = Phase::Draining;
            drain_obligation_ = DrainObligation::Quarantine;
            return runtime::RuntimeWorkStep::draining(drain_);
        }
        phase_ = Phase::Terminal;
        return runtime::RuntimeWorkStep::failed();
    }

    if (phase_ == Phase::Preparing) {
        if (!prepare_terminal_.has_value()) {
            return wait_or_drain(context, AcquisitionFailurePhase::Prepare);
        }
        if (context.stop().stop_requested()) {
            return fail(
                AcquisitionFailurePhase::Prepare,
                AcquisitionFailureReason::StopRequested);
        }
        if (context.deadline().expired()) {
            return fail(
                AcquisitionFailurePhase::Prepare,
                AcquisitionFailureReason::DeadlineExpired);
        }
        if (!consume_transition_budget(context)) {
            return fail(
                AcquisitionFailurePhase::Prepare,
                AcquisitionFailureReason::BudgetExhausted);
        }

        auto terminal = std::move(*prepare_terminal_);
        prepare_terminal_.reset();
        if (auto* failed = std::get_if<board::PrepareFailed>(&terminal)) {
            return fail_board_terminal(
                AcquisitionFailurePhase::Prepare,
                AcquisitionFailureReason::BoardPrepareFailed,
                failed->error);
        }
        if (std::holds_alternative<board::PrepareDraining>(terminal)) {
            auto draining =
                std::get<board::PrepareDraining>(std::move(terminal));
            board_prepare_drain_owner_.emplace(std::move(draining.owner));
            failure_ = AcquisitionFailure{
                AcquisitionFailurePhase::Prepare,
                AcquisitionFailureReason::BoardPrepareDraining,
                prepare_call_,
                prepared_,
                run_,
                generation_,
                false,
                board::BoardErrc::ContractViolation,
                AcquisitionRetryClass::AfterRecovery,
                AcquisitionSafetyImpact::ResourceIsolationRequired};
            phase_ = Phase::Draining;
            drain_obligation_ = DrainObligation::Quarantine;
            return runtime::RuntimeWorkStep::draining(drain_);
        }

        auto prepared = std::move(std::get<board::PrepareSucceeded>(terminal).execution);
        prepared_manifest_.emplace(std::move(prepared.manifest));
        const auto manifest = prepared_manifest_->manifest();
        prepared_ = manifest.prepared_id;
        manifest_ = manifest.id;
        board_session_ = manifest.session_id;
        capability_revision_ = manifest.capability_revision;
        if (!resources_.narrow_to(manifest)) {
            return begin_prepared_discard(
                std::move(prepared.start_token),
                board::DiscardPreparedReason::ManifestOutsideAdmission,
                AcquisitionFailurePhase::ManifestFinalization,
                AcquisitionFailureReason::ManifestOutsideAdmission,
                nullptr);
        }
        builder_.emplace(manifest, run_, generation_);
        if (builder_->error().has_value()) {
            return begin_prepared_discard(
                std::move(prepared.start_token),
                board::DiscardPreparedReason::ManifestOutsideAdmission,
                AcquisitionFailurePhase::ManifestFinalization,
                AcquisitionFailureReason::ManifestOutsideAdmission,
                nullptr);
        }

        auto start_authorization = board::StartAuthorization::issue(
            manifest.session_id,
            manifest.prepared_id,
            manifest.manifest_digest,
            manifest.operational_epoch,
            continuation_);
        auto submission = execution_->begin_run(
            board_reservation_,
            run_,
            generation_,
            std::move(prepared.start_token),
            std::move(start_authorization),
            std::move(delivery_),
            board::BoardRunSinkRegistration{*this});
        if (auto* rejected = std::get_if<board::RunRejected>(&submission)) {
            auto reclaimed = std::move(rejected->reclaimed);
            const auto error = rejected->error;
            rejected_start_authorization_.emplace(
                std::move(reclaimed.authorization));
            rejected_delivery_.emplace(std::move(reclaimed.delivery));
            rejected_run_sink_.emplace(std::move(reclaimed.sink));
            return begin_prepared_discard(
                std::move(reclaimed.prepared),
                board::DiscardPreparedReason::RunRejected,
                AcquisitionFailurePhase::Run,
                AcquisitionFailureReason::BoardRejected,
                &error);
        }
        const auto accepted = std::get<board::RunAccepted>(submission);
        if (accepted.run != run_ || accepted.generation != generation_) {
            return drain_contract_violation(
                AcquisitionFailurePhase::Run,
                DrainObligation::RunTerminal);
        }
        phase_ = Phase::Acquiring;
        return runtime::RuntimeWorkStep::running();
    }

    if (phase_ == Phase::Acquiring) {
        while (auto chunk = ingress_.pop()) {
            if (!builder_.has_value() ||
                builder_->accept(std::move(*chunk)) !=
                    board::ChunkIngressDisposition::Accepted) {
                callback_contract_violation_ = true;
            }
        }
        if (!run_terminal_.has_value()) {
            return wait_or_drain(context, AcquisitionFailurePhase::Run);
        }
        if (context.stop().stop_requested()) {
            return fail(
                AcquisitionFailurePhase::Run,
                AcquisitionFailureReason::StopRequested);
        }
        if (context.deadline().expired()) {
            return fail(
                AcquisitionFailurePhase::Run,
                AcquisitionFailureReason::DeadlineExpired);
        }
        if (!consume_transition_budget(context)) {
            return fail(
                AcquisitionFailurePhase::Run,
                AcquisitionFailureReason::BudgetExhausted);
        }

        const auto terminal = *run_terminal_;
        run_terminal_.reset();
        if (!builder_.has_value() || !builder_->record_terminal(terminal)) {
            callback_contract_violation_ = true;
        }
        if (callback_contract_violation_ || terminal.run_id != run_ ||
            terminal.generation != generation_) {
            return fail(
                AcquisitionFailurePhase::Run,
                AcquisitionFailureReason::BoardContractViolation);
        }
        if (terminal.kind != board::RunTerminalKind::Completed) {
            return fail(
                AcquisitionFailurePhase::Run,
                AcquisitionFailureReason::BoardTerminalFailed);
        }
        auto candidate_result = builder_->seal(
            snapshot_id_, logical_sweep_id_, work_, intent_.digest);
        if (!candidate_result.has_value()) {
            return fail(
                AcquisitionFailurePhase::CandidateSealing,
                candidate_result.error().code ==
                        NetworkObservationErrc::IncompleteCoverage
                    ? AcquisitionFailureReason::IncompleteObservationSet
                    : AcquisitionFailureReason::BoardContractViolation);
        }
        success_.emplace(AcquisitionSucceeded{
            std::move(candidate_result).take_value(),
            AOnlyCompletionOwners{std::move(resources_), snapshot_id_}});
        phase_ = Phase::Terminal;
        return runtime::RuntimeWorkStep::completed();
    }

    return fail(
        AcquisitionFailurePhase::Admission,
        AcquisitionFailureReason::BoardContractViolation);
}

runtime::RuntimeDrainStep AcquisitionEngine::resume_drain(
    runtime::ExecutionContext& context) noexcept {
    (void)context;
    if (phase_ != Phase::Draining) {
        return runtime::RuntimeDrainStep::cleanup_failed();
    }
    if (drain_obligation_ == DrainObligation::Quarantine) {
        // 当前 Board seam 没有可证明底软排空完成的后续 terminal。把 drain owner、
        // 本地资源和 execution reservation 留在 Engine，由 L2 在 Quarantined
        // terminal 后继续隔离；绝不能谎报 Drained 并复用容量。
        phase_ = Phase::Terminal;
        return runtime::RuntimeDrainStep::quarantined();
    }
    if (drain_obligation_ == DrainObligation::PrepareTerminal) {
        if (!prepare_terminal_.has_value()) {
            return runtime::RuntimeDrainStep::running();
        }
        if (!std::holds_alternative<board::PrepareFailed>(*prepare_terminal_)) {
            // PrepareSucceeded 仍携带已准备资源；PrepareDraining 则携带尚未
            // 完成的排空义务。当前 Board seam 对两者都没有 discard/drain
            // 完成证明，因此错误 Accepted 身份不能借“收到 terminal”释放容量。
            if (std::holds_alternative<board::PrepareDraining>(*prepare_terminal_)) {
                auto draining = std::get<board::PrepareDraining>(
                    std::move(*prepare_terminal_));
                prepare_terminal_.reset();
                board_prepare_drain_owner_.emplace(std::move(draining.owner));
            }
            drain_obligation_ = DrainObligation::Quarantine;
            phase_ = Phase::Terminal;
            return runtime::RuntimeDrainStep::quarantined();
        }
    } else if (drain_obligation_ == DrainObligation::RunTerminal) {
        if (!run_terminal_.has_value()) {
            return runtime::RuntimeDrainStep::running();
        }
    } else {
        return runtime::RuntimeDrainStep::cleanup_failed();
    }

    // PrepareFailed 的 cleanup evidence 或匹配的 Run terminal 才能证明 Board
    // 已不再持有本工作资源，此时才允许终结上层 owner 并报告 Drained。
    (void)resources_.finalize_failure();
    phase_ = Phase::Terminal;
    return runtime::RuntimeDrainStep::drained();
}

bool AcquisitionEngine::finalize_failure_owners() noexcept {
    return resources_.finalize_failure();
}

std::optional<AcquisitionSucceeded> AcquisitionEngine::take_success() noexcept {
    if (!success_.has_value()) {
        return std::nullopt;
    }
    auto result = std::optional<AcquisitionSucceeded>{std::move(*success_)};
    success_.reset();
    return result;
}

void AcquisitionEngine::on_terminal(
    board::PrepareTerminal&& terminal) noexcept {
    if ((phase_ != Phase::Preparing && phase_ != Phase::Draining) ||
        prepare_terminal_.has_value()) {
        callback_contract_violation_ = true;
        return;
    }
    prepare_terminal_.emplace(std::move(terminal));
}

void AcquisitionEngine::on_terminal(
    board::DiscardPreparedTerminal&& terminal) noexcept {
    if (phase_ != Phase::DiscardingPrepared ||
        discard_terminal_.has_value()) {
        discard_contract_violation_ = true;
        return;
    }
    discard_terminal_.emplace(std::move(terminal));
}

void AcquisitionEngine::on_phase(
    const board::BoardRunPhaseEvent& event) noexcept {
    if ((phase_ != Phase::Acquiring && phase_ != Phase::Draining) ||
        event.run_id != run_ || event.generation != generation_) {
        callback_contract_violation_ = true;
    }
}

board::ChunkIngressDisposition AcquisitionEngine::on_chunk(
    board::ReceiverObservationChunk&& chunk) noexcept {
    // BoardRunSink 是最外层无条件所有权边界。即使 Adapter 违反时序或身份，
    // 也必须先接管 payload，再用 disposition 要求其停止后续交付。
    auto owned = std::move(chunk);
    if (phase_ != Phase::Acquiring || run_terminal_.has_value() ||
        !builder_.has_value()) {
        callback_contract_violation_ = true;
        return board::ChunkIngressDisposition::AbortRunProtocolViolation;
    }
    const auto disposition = ingress_.push(std::move(owned));
    if (disposition != board::ChunkIngressDisposition::Accepted) {
        callback_contract_violation_ = true;
    }
    return disposition;
}

void AcquisitionEngine::on_terminal(
    board::BoardRunTerminal&& terminal) noexcept {
    if ((phase_ != Phase::Acquiring && phase_ != Phase::Draining) ||
        terminal.run_id != run_ || terminal.generation != generation_ ||
        run_terminal_.has_value()) {
        // 错误身份的 terminal 不能解除原请求的 callback 生命周期义务；保留
        // sink 与 execution reservation，等待匹配 RunId/Generation 的真实终态。
        callback_contract_violation_ = true;
        return;
    }
    run_terminal_.emplace(std::move(terminal));
}

bool AcquisitionEngine::consume_transition_budget(
    runtime::ExecutionContext& context) noexcept {
    return context.budget().try_consume(1U);
}

runtime::RuntimeWorkStep AcquisitionEngine::fail(
    AcquisitionFailurePhase phase,
    AcquisitionFailureReason reason) noexcept {
    failure_ = AcquisitionFailure{
        phase,
        reason,
        prepare_call_,
        prepared_,
        run_,
        generation_,
        false,
        board::BoardErrc::ContractViolation,
        retry_for_failure(reason),
        safety_for_failure(phase, reason)};
    attach_manifest_context(
        failure_, manifest_, board_session_, capability_revision_);
    phase_ = Phase::Terminal;
    return runtime::RuntimeWorkStep::failed();
}

runtime::RuntimeWorkStep AcquisitionEngine::fail_board_rejection(
    AcquisitionFailurePhase phase,
    board::BoardError error) noexcept {
    failure_ = AcquisitionFailure{
        phase,
        AcquisitionFailureReason::BoardRejected,
        prepare_call_,
        prepared_,
        run_,
        generation_,
        true,
        error.code,
        retry_for_board_error(error.code),
        AcquisitionSafetyImpact::NoRunAccepted};
    attach_manifest_context(
        failure_, manifest_, board_session_, capability_revision_);
    phase_ = Phase::Terminal;
    return runtime::RuntimeWorkStep::failed();
}

runtime::RuntimeWorkStep AcquisitionEngine::fail_board_terminal(
    AcquisitionFailurePhase phase,
    AcquisitionFailureReason reason,
    board::BoardError error) noexcept {
    failure_ = AcquisitionFailure{
        phase,
        reason,
        prepare_call_,
        prepared_,
        run_,
        generation_,
        true,
        error.code,
        retry_for_board_error(error.code),
        safety_for_failure(phase, reason)};
    attach_manifest_context(
        failure_, manifest_, board_session_, capability_revision_);
    phase_ = Phase::Terminal;
    return runtime::RuntimeWorkStep::failed();
}

runtime::RuntimeWorkStep AcquisitionEngine::begin_prepared_discard(
    board::PreparedStartToken&& prepared,
    board::DiscardPreparedReason discard_reason,
    AcquisitionFailurePhase failure_phase,
    AcquisitionFailureReason failure_reason,
    const board::BoardError* board_error) noexcept {
    const bool has_board_error = board_error != nullptr;
    const auto error = has_board_error
        ? board_error->code
        : board::BoardErrc::ContractViolation;
    failure_ = AcquisitionFailure{
        failure_phase,
        failure_reason,
        prepare_call_,
        prepared_,
        run_,
        generation_,
        has_board_error,
        error,
        has_board_error ? retry_for_board_error(error)
                        : retry_for_failure(failure_reason),
        AcquisitionSafetyImpact::NoRunAccepted};
    attach_manifest_context(
        failure_, manifest_, board_session_, capability_revision_);

    auto submission = execution_->begin_discard_prepared(
        board_reservation_,
        std::move(prepared),
        board::DiscardPreparedRequest{discard_reason},
        board::DiscardPreparedSinkRegistration{*this});
    if (auto* rejected =
            std::get_if<board::DiscardPreparedRejected>(&submission)) {
        const auto discard_error = rejected->error;
        quarantined_prepared_.emplace(
            std::move(rejected->reclaimed.prepared));
        quarantined_discard_sink_.emplace(
            std::move(rejected->reclaimed.sink));
        failure_.reason = AcquisitionFailureReason::BoardContractViolation;
        failure_.has_board_error = true;
        failure_.board_error = discard_error.code;
        failure_.retry = AcquisitionRetryClass::AfterRecovery;
        failure_.safety =
            AcquisitionSafetyImpact::ResourceIsolationRequired;
        phase_ = Phase::Draining;
        drain_obligation_ = DrainObligation::Quarantine;
        return runtime::RuntimeWorkStep::draining(drain_);
    }

    const auto accepted =
        std::get<board::DiscardPreparedAccepted>(submission);
    if (accepted.prepared != prepared_) {
        // Adapter 已取得 token/sink，错误同步身份不能解除 callback 生命周期；
        // 等真实 terminal 后转入 Quarantine，不能立即释放 reservation。
        discard_contract_violation_ = true;
    }
    phase_ = Phase::DiscardingPrepared;
    return runtime::RuntimeWorkStep::running();
}

runtime::RuntimeWorkStep AcquisitionEngine::wait_or_drain(
    runtime::ExecutionContext& context,
    AcquisitionFailurePhase phase) noexcept {
    AcquisitionFailureReason reason{AcquisitionFailureReason::BudgetExhausted};
    bool should_drain{false};
    if (context.stop().stop_requested()) {
        reason = AcquisitionFailureReason::StopRequested;
        should_drain = true;
    } else if (context.deadline().expired()) {
        reason = AcquisitionFailureReason::DeadlineExpired;
        should_drain = true;
    } else if (!consume_transition_budget(context)) {
        should_drain = true;
    }
    if (!should_drain) {
        return runtime::RuntimeWorkStep::running();
    }

    failure_ = AcquisitionFailure{
        phase,
        reason,
        prepare_call_,
        prepared_,
        run_,
        generation_,
        false,
        board::BoardErrc::ContractViolation,
        retry_for_failure(reason),
        AcquisitionSafetyImpact::ResourceIsolationRequired};
    attach_manifest_context(
        failure_, manifest_, board_session_, capability_revision_);
    phase_ = Phase::Draining;
    drain_obligation_ = phase == AcquisitionFailurePhase::Prepare
        ? DrainObligation::PrepareTerminal
        : DrainObligation::RunTerminal;
    return runtime::RuntimeWorkStep::draining(drain_);
}

runtime::RuntimeWorkStep AcquisitionEngine::drain_contract_violation(
    AcquisitionFailurePhase phase,
    DrainObligation obligation) noexcept {
    // Accepted 已把非 owning sink 交给 Adapter；即使同步回执身份错误，也必须
    // 保活 Engine 到该次真实 terminal，不能把 Adapter 违约降级成普通 Failed。
    failure_ = AcquisitionFailure{
        phase,
        AcquisitionFailureReason::BoardContractViolation,
        prepare_call_,
        prepared_,
        run_,
        generation_,
        false,
        board::BoardErrc::ContractViolation,
        AcquisitionRetryClass::AfterRecovery,
        AcquisitionSafetyImpact::ResourceIsolationRequired};
    attach_manifest_context(
        failure_, manifest_, board_session_, capability_revision_);
    phase_ = Phase::Draining;
    drain_obligation_ = obligation;
    return runtime::RuntimeWorkStep::draining(drain_);
}

}  // namespace vna::acquisition
