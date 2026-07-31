#include <gtest/gtest.h>

#include <array>
#include <cstdlib>
#include <filesystem>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

#include <vna/platform/state_directory.hpp>

namespace vna::platform {
namespace {

// These contract tests use only the public platform interface.

#ifdef _WIN32

class ScopedEnvironmentVariable {
public:
    ScopedEnvironmentVariable(std::string name, std::string value)
        : name_(std::move(name)) {
        if (const auto* previous = std::getenv(name_.c_str())) {
            previous_ = previous;
        }
        if (_putenv_s(name_.c_str(), value.c_str()) != 0) {
            throw std::runtime_error("failed to set environment variable");
        }
    }

    ~ScopedEnvironmentVariable() {
        const auto value = previous_.value_or("");
        _putenv_s(name_.c_str(), value.c_str());
    }

private:
    std::string name_;
    std::optional<std::string> previous_;
};

TEST(StateDirectoryTest, ResolvesWindowsLocalAppDataDirectory) {
    const std::filesystem::path root{R"(C:\Users\tester\AppData\Local)"};
    const StateDirectoryInputs inputs{.localAppData = root};

    EXPECT_EQ(
        resolveStateDirectory("VectorNetworkAnalyzer", inputs),
        root / "VectorNetworkAnalyzer");
}

TEST(StateDirectoryTest, RejectsMissingWindowsLocalAppDataDirectory) {
    EXPECT_THROW(
        resolveStateDirectory("VectorNetworkAnalyzer", {}),
        std::runtime_error);
}

TEST(StateDirectoryTest, RejectsRelativeWindowsLocalAppDataDirectory) {
    const StateDirectoryInputs inputs{.localAppData = "relative-state"};

    EXPECT_THROW(
        resolveStateDirectory("VectorNetworkAnalyzer", inputs),
        std::invalid_argument);
}

TEST(StateDirectoryTest, RejectsUnsafeApplicationNames) {
    const StateDirectoryInputs inputs{
        .localAppData = R"(C:\Users\tester\AppData\Local)",
    };
    constexpr std::array<std::string_view, 6> invalidNames{
        "",
        ".",
        "..",
        "/absolute",
        "nested/name",
        R"(nested\name)",
    };

    for (const auto name : invalidNames) {
        SCOPED_TRACE(name);
        EXPECT_THROW(
            resolveStateDirectory(name, inputs),
            std::invalid_argument);
    }
}

TEST(StateDirectoryTest, ReadsWindowsLocalAppDataEnvironment) {
    const auto root = std::filesystem::temp_directory_path() /
                      "vna-platform-state-directory-test";
    const ScopedEnvironmentVariable environment{
        "LOCALAPPDATA",
        root.string(),
    };

    EXPECT_EQ(
        currentUserStateDirectory("VectorNetworkAnalyzer"),
        root / "VectorNetworkAnalyzer");
}

#else

class ScopedEnvironmentVariable {
public:
    ScopedEnvironmentVariable(std::string name, std::string value)
        : name_(std::move(name)) {
        if (const auto* previous = std::getenv(name_.c_str())) {
            previous_ = previous;
        }
        if (setenv(name_.c_str(), value.c_str(), 1) != 0) {
            throw std::runtime_error("failed to set environment variable");
        }
    }

    ~ScopedEnvironmentVariable() {
        if (previous_) {
            setenv(name_.c_str(), previous_->c_str(), 1);
        } else {
            unsetenv(name_.c_str());
        }
    }

private:
    std::string name_;
    std::optional<std::string> previous_;
};

TEST(StateDirectoryTest, PrefersLinuxXdgStateDirectory) {
    const StateDirectoryInputs inputs{
        .xdgStateHome = "/state",
        .homeDirectory = "/home/tester",
    };

    EXPECT_EQ(
        resolveStateDirectory("VectorNetworkAnalyzer", inputs),
        "/state/VectorNetworkAnalyzer");
}

TEST(StateDirectoryTest, FallsBackToLinuxHomeStateDirectory) {
    const StateDirectoryInputs inputs{.homeDirectory = "/home/tester"};

    EXPECT_EQ(
        resolveStateDirectory("VectorNetworkAnalyzer", inputs),
        "/home/tester/.local/state/VectorNetworkAnalyzer");
}

TEST(StateDirectoryTest, RejectsMissingLinuxStateDirectory) {
    EXPECT_THROW(
        resolveStateDirectory("VectorNetworkAnalyzer", {}),
        std::runtime_error);
}

TEST(StateDirectoryTest, RejectsRelativeLinuxStateDirectories) {
    const StateDirectoryInputs relativeXdg{.xdgStateHome = "state"};
    const StateDirectoryInputs relativeHome{.homeDirectory = "home/tester"};

    EXPECT_THROW(
        resolveStateDirectory("VectorNetworkAnalyzer", relativeXdg),
        std::invalid_argument);
    EXPECT_THROW(
        resolveStateDirectory("VectorNetworkAnalyzer", relativeHome),
        std::invalid_argument);
}

TEST(StateDirectoryTest, RejectsUnsafeLinuxApplicationNames) {
    const StateDirectoryInputs inputs{.xdgStateHome = "/state"};
    constexpr std::array<std::string_view, 6> invalidNames{
        "", ".", "..", "/absolute", "nested/name", R"(nested\name)"};

    for (const auto name : invalidNames) {
        SCOPED_TRACE(name);
        EXPECT_THROW(
            resolveStateDirectory(name, inputs),
            std::invalid_argument);
    }
}

TEST(StateDirectoryTest, ReadsLinuxXdgStateEnvironment) {
    const auto root = std::filesystem::temp_directory_path() /
                      "vna-platform-state-directory-test";
    const ScopedEnvironmentVariable environment{
        "XDG_STATE_HOME",
        root.string(),
    };

    EXPECT_EQ(
        currentUserStateDirectory("VectorNetworkAnalyzer"),
        root / "VectorNetworkAnalyzer");
}

#endif

}  // namespace
}  // namespace vna::platform
