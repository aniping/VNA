#pragma once

#if !defined(VNA_ENABLE_STORE_CONTRACT_TEST_HOOKS)
#error "Store contract test access requires the test-only Store build flag"
#endif

#include "runtime/resource/store/instrument_store.h"

namespace vna::store {

/// 只供合同测试注入一次性正式 A 发布拒绝、终态提交完整性故障或畸形回执的
/// Store friend 访问器；关闭测试的产品构建不包含此类型及其故障入口。
class InstrumentStoreContractTestAccess final {
public:
    /// 令下一次身份合法的 A candidate 在 schema/domain validation 阶段被拒绝。
    /// @param store 被测试的 Store；不转移所有权，故障被一次有效提交消费。
    static void reject_next_completed_sweep_validation(
        InstrumentStore& store) noexcept {
        store.next_completed_sweep_commit_fault_ =
            StoreErrc::CandidateValidationRejected;
    }

    /// 令下一次身份合法且已完成 staging 的 A candidate 在切换 revision 前被拒绝。
    /// @param store 被测试的 Store；不转移所有权，故障被一次有效提交消费。
    static void reject_next_completed_sweep_write(
        InstrumentStore& store) noexcept {
        store.next_completed_sweep_commit_fault_ =
            StoreErrc::CandidateWriteRejected;
    }

    /// 令下一次 state-only acquisition Failed 提交报告 Store 完整性故障。
    /// @param store 被测试的 Store；不转移所有权，故障被一次提交消费。
    static void fail_next_acquisition_failure_commit(
        InstrumentStore& store) noexcept {
        store.fail_next_acquisition_failure_commit_ = true;
    }

    /// 令下一次 state-only Failed 返回错误 Operation/零 revision 的伪成功回执。
    /// @param store 被测试的 Store；不转移所有权，故障被一次提交消费。
    static void return_malformed_acquisition_failure_receipt(
        InstrumentStore& store) noexcept {
        store.return_malformed_acquisition_failure_receipt_ = true;
    }
};

}  // namespace vna::store
