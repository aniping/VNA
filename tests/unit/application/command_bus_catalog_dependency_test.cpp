#include <gtest/gtest.h>

#include <type_traits>

#include <vna/application/command_bus.hpp>
#include <vna/application/single_sweep_command_handler.hpp>
#include <vna/application/trace_publication_catalog.hpp>

namespace vna::application {
namespace {

static_assert(std::is_constructible_v<
    CommandBus,
    InstrumentId,
    SingleSweepCommandHandler&,
    TracePublicationCatalog&>);
static_assert(!std::is_constructible_v<
    CommandBus,
    InstrumentId,
    SingleSweepCommandHandler&>);

TEST(CommandBusCatalogDependencyTest, RequiresAnExplicitPublicationCatalog) {
    SUCCEED();
}

}  // namespace
}  // namespace vna::application
