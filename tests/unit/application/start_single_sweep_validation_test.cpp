#include <gtest/gtest.h>

#include <utility>
#include <variant>

#include <vna/application/command_bus.hpp>
#include <vna/test/stopped_single_sweep_handler.hpp>

namespace vna::application {
namespace {

enum class UnsupportedScenario {
    S21,
    MissingTrace,
    Phase,
    Smith,
    MultipleMeasurements,
    MultipleTraces,
};

template <typename Value>
Value successValue(const CommandResult& result) {
    return std::get<Value>(std::get<CommandSuccess>(result.outcome).value);
}

const ApplicationError* applicationError(const CommandResult& result) {
    const auto* error = std::get_if<CommandError>(&result.outcome);
    return error == nullptr ? nullptr : std::get_if<ApplicationError>(error);
}

class ValidationHarness {
public:
    ValidationHarness()
        : bus_(InstrumentId{"instrument-1"},
               vna::test::stoppedSingleSweepHandler()) {}

    void configure(UnsupportedScenario scenario) {
        channel_ = successValue<domain::ChannelId>(dispatch(
            "channel", CreateChannelCommand{.sweep = validSweep()}));
        const auto type = scenario == UnsupportedScenario::S21
            ? domain::MeasurementType::S21
            : domain::MeasurementType::S11;
        const auto measurement = successValue<domain::MeasurementId>(dispatch(
            "measurement", CreateMeasurementCommand{channel_, type}));
        if (scenario == UnsupportedScenario::MissingTrace) {
            return;
        }
        const auto window = successValue<display_model::WindowId>(
            dispatch("window", CreateWindowCommand{}));
        createTrace("trace", window, measurement, formatFor(scenario));
        if (scenario == UnsupportedScenario::MultipleMeasurements) {
            static_cast<void>(dispatch(
                "measurement-2",
                CreateMeasurementCommand{
                    channel_, domain::MeasurementType::S11}));
        }
        if (scenario == UnsupportedScenario::MultipleTraces) {
            createTrace("trace-2", window, measurement,
                        display_model::TraceFormat::LogMagnitude);
        }
    }

    CommandResult start(domain::ChannelId channelId) {
        return dispatch("start", StartSingleSweepCommand{channelId});
    }

    [[nodiscard]] domain::ChannelId channel() const { return channel_; }
    [[nodiscard]] const CommandBus& bus() const { return bus_; }

private:
    static constexpr domain::SweepSettings validSweep() {
        return {1'000'000, 2'000'000, 5, 1'000, -10.0};
    }

    static display_model::TraceFormat formatFor(
        UnsupportedScenario scenario) {
        if (scenario == UnsupportedScenario::Phase) {
            return display_model::TraceFormat::Phase;
        }
        if (scenario == UnsupportedScenario::Smith) {
            return display_model::TraceFormat::Smith;
        }
        return display_model::TraceFormat::LogMagnitude;
    }

    void createTrace(
        const char* id,
        display_model::WindowId window,
        domain::MeasurementId measurement,
        display_model::TraceFormat format) {
        static_cast<void>(dispatch(
            id, CreateTraceCommand{window, measurement, format}));
    }

    CommandResult dispatch(const char* id, CommandPayload payload) {
        return bus_.dispatch(CommandEnvelope{
            .commandId = CommandId{id},
            .sessionId = SessionId{"session-1"},
            .instrumentId = InstrumentId{"instrument-1"},
            .origin = CommandOrigin::Web,
            .expectedStateRevision = bus_.snapshot().stateRevision,
            .payload = std::move(payload),
        });
    }

    domain::ChannelId channel_{0};
    CommandBus bus_;
};

class UnsupportedSweepConfigurationTest
    : public ::testing::TestWithParam<UnsupportedScenario> {};

TEST_P(UnsupportedSweepConfigurationTest, RejectsBeforeExecutorAdmission) {
    ValidationHarness harness;
    harness.configure(GetParam());
    const auto revision = harness.bus().snapshot().stateRevision;

    const auto result = harness.start(harness.channel());

    ASSERT_NE(applicationError(result), nullptr);
    EXPECT_EQ(applicationError(result)->code,
              ApplicationErrorCode::UnsupportedSweepConfiguration);
    EXPECT_EQ(result.stateRevision, revision);
    EXPECT_EQ(harness.bus().snapshot().stateRevision, revision);
}

INSTANTIATE_TEST_SUITE_P(
    UnsupportedShapes,
    UnsupportedSweepConfigurationTest,
    ::testing::Values(
        UnsupportedScenario::S21,
        UnsupportedScenario::MissingTrace,
        UnsupportedScenario::Phase,
        UnsupportedScenario::Smith,
        UnsupportedScenario::MultipleMeasurements,
        UnsupportedScenario::MultipleTraces));

TEST(StartSingleSweepValidationTest, ReportsMissingChannelAsDomainError) {
    ValidationHarness harness;

    const auto result = harness.start(domain::ChannelId{99});

    const auto* error = std::get_if<CommandError>(&result.outcome);
    ASSERT_NE(error, nullptr);
    const auto* domainError = std::get_if<domain::DomainError>(error);
    ASSERT_NE(domainError, nullptr);
    EXPECT_EQ(domainError->code, domain::DomainErrorCode::ChannelNotFound);
    EXPECT_EQ(result.stateRevision, 0U);
}

}  // namespace
}  // namespace vna::application
