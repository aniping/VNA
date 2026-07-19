#pragma once

#if !defined(VNA_ENABLE_RUNTIME_CONTRACT_TEST_HOOKS)
#error "Runtime contract test access requires the test-only Runtime build flag"
#endif

#include "runtime/function/operation/operation_runtime.h"

namespace vna::runtime {

/// 只供合同测试触发 OperationRuntime 不可达防御分支的 friend 访问器。
class OperationRuntimeContractTestAccess final {
public:
    /// 令下一次已经通过正常 reserve_work() 的 dispatch() 返回 InvalidPermit。
    /// @param runtime 被测试的 Runtime；不转移所有权，标志在一次 dispatch 后清除。
    static void reject_next_dispatch(OperationRuntime& runtime) noexcept {
        runtime.reject_next_dispatch_for_contract_test_ = true;
    }
};

}  // namespace vna::runtime
