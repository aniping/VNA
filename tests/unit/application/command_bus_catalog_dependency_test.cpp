#include <gtest/gtest.h>

#include <type_traits>

#include <vna/application/command_bus.hpp>
#include <vna/application/single_sweep_command_handler.hpp>
#include <vna/application/sweep_runtime.hpp>

namespace vna::application {
namespace {

static_assert(std::is_constructible_v<
    CommandBus,
    InstrumentId,
    SingleSweepCommandHandler&,
    SweepRuntime&>);
static_assert(!std::is_constructible_v<
    CommandBus,
    InstrumentId,
    SingleSweepCommandHandler&>);

TEST(CommandBusCatalogDependencyTest, RequiresTheUniqueSweepRuntime) {
    SUCCEED();
}

}  // namespace
}  // namespace vna::application
