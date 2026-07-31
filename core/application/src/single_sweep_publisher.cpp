#include "single_sweep_publisher_internal.hpp"

#include <exception>
#include <utility>

namespace vna::application::internal {

std::optional<OperationFailure> publishTraceDisplayFrame(
    const TraceDisplayPublisher& publish,
    TraceDisplayFrame frame) noexcept {
    try {
        const auto published = publish(std::move(frame));
        if (published.hasValue()) {
            return std::nullopt;
        }
        return OperationFailure{
            .code = SingleSweepFailureCode::TraceDisplayPublishFailed,
            .cause = published.error()};
    } catch (...) {
        return OperationFailure{
            .code = SingleSweepFailureCode::TraceDisplayPublishFailed,
            .cause = std::current_exception()};
    }
}

}  // namespace vna::application::internal
