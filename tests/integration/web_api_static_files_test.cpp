#include <gtest/gtest.h>

#include <httplib.h>

#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

#include <vna/web_api/web_api.hpp>

namespace vna::web_api {
namespace {

std::filesystem::path createTemporaryDirectory() {
    static std::atomic<unsigned long long> sequence{0};
    const auto tick = std::chrono::steady_clock::now()
                          .time_since_epoch()
                          .count();
    const auto path = std::filesystem::temp_directory_path() /
        ("vna-web-api-" + std::to_string(tick) + "-" +
         std::to_string(sequence.fetch_add(1)));
    std::filesystem::create_directory(path);
    return path;
}

void writeFile(const std::filesystem::path& path, const std::string& contents) {
    std::ofstream stream{path, std::ios::binary};
    stream.exceptions(std::ios::failbit | std::ios::badbit);
    stream << contents;
}

std::error_code createDirectoryLink(
    const std::filesystem::path& target,
    const std::filesystem::path& link) {
#ifdef _WIN32
    if (CreateSymbolicLinkW(
            link.c_str(), target.c_str(), SYMBOLIC_LINK_FLAG_DIRECTORY)) {
        return {};
    }
    return {static_cast<int>(GetLastError()), std::system_category()};
#else
    std::error_code error;
    std::filesystem::create_directory_symlink(target, link, error);
    return error;
#endif
}

class WebApiStaticFilesTest : public ::testing::Test {
protected:
    WebApiStaticFilesTest()
        : directory_(createTemporaryDirectory()),
          webRoot_(directory_ / "web") {
        std::filesystem::create_directories(webRoot_ / "assets");
        writeFile(webRoot_ / "index.html", "<h1>Vector Network Analyzer</h1>");
        writeFile(webRoot_ / "assets" / "app.js", "console.log('vna');");
    }

    ~WebApiStaticFilesTest() override {
        std::error_code error;
        std::filesystem::remove_all(directory_, error);
    }

    void startServer(const std::filesystem::path& root) {
        webApi_ = std::make_unique<WebApi>(commandBus_, root);
        port_ = webApi_->bindToAnyPort("127.0.0.1");
        ASSERT_GT(port_, 0);
        serverThread_ = std::thread([this] {
            static_cast<void>(webApi_->listenAfterBind());
        });
        webApi_->waitUntilReady();
    }

    httplib::Result get(const std::string& path) const {
        return httplib::Client{"127.0.0.1", port_}.Get(path);
    }

    void expectInvalidRoot(const std::filesystem::path& root) {
        EXPECT_THROW(WebApi(commandBus_, root), std::invalid_argument);
    }

    void expectNotFound(const httplib::Result& response) {
        ASSERT_TRUE(response);
        EXPECT_EQ(response->status, httplib::StatusCode::NotFound_404);
        EXPECT_NE(response->body, "outside-web-root");
    }

    void TearDown() override {
        if (webApi_) {
            webApi_->stop();
        }
        if (serverThread_.joinable()) {
            serverThread_.join();
        }
    }

    application::CommandBus commandBus_{
        application::InstrumentId{"instrument-1"}};
    std::filesystem::path directory_;
    std::filesystem::path webRoot_;
    std::unique_ptr<WebApi> webApi_;
    int port_{-1};
    std::thread serverThread_;
};

TEST_F(WebApiStaticFilesTest, ServesIndexRoutesWithoutCaching) {
    startServer(webRoot_);
    const auto response = get("/");
    const auto alias = get("/index.html");

    ASSERT_TRUE(response);
    EXPECT_EQ(response->status, httplib::StatusCode::OK_200);
    EXPECT_EQ(response->body, "<h1>Vector Network Analyzer</h1>");
    EXPECT_EQ(response->get_header_value("Content-Type"),
              "text/html; charset=utf-8");
    EXPECT_EQ(response->get_header_value("Cache-Control"), "no-cache");
    ASSERT_TRUE(alias);
    EXPECT_EQ(alias->body, response->body);
    EXPECT_EQ(alias->get_header_value("Cache-Control"), "no-cache");
}

TEST_F(WebApiStaticFilesTest, ServesAssetsWithImmutableCaching) {
    startServer(webRoot_);
    const auto response = get("/assets/app.js");

    ASSERT_TRUE(response);
    EXPECT_EQ(response->status, httplib::StatusCode::OK_200);
    EXPECT_EQ(response->body, "console.log('vna');");
    EXPECT_EQ(response->get_header_value("Content-Type"), "text/javascript");
    EXPECT_EQ(response->get_header_value("Cache-Control"),
              "public, max-age=31536000, immutable");
}

TEST_F(WebApiStaticFilesTest, ApiRoutesCannotBeShadowedByWebFiles) {
    std::filesystem::create_directories(webRoot_ / "api" / "v1");
    writeFile(webRoot_ / "api" / "v1" / "health", "not-the-api");
    startServer(webRoot_);
    const auto response = get("/api/v1/health");

    ASSERT_TRUE(response);
    EXPECT_EQ(response->status, httplib::StatusCode::OK_200);
    EXPECT_EQ(response->body, R"({"status":"ok"})");
    EXPECT_EQ(response->get_header_value("Content-Type"), "application/json");
}

TEST_F(WebApiStaticFilesTest, RejectsInvalidWebRootsBeforeServerStart) {
    const auto missing = directory_ / "missing";
    expectInvalidRoot(missing);

    const auto missingIndex = directory_ / "missing-index";
    std::filesystem::create_directories(missingIndex / "assets");
    expectInvalidRoot(missingIndex);

    const auto indexDirectory = directory_ / "index-directory";
    std::filesystem::create_directories(indexDirectory / "index.html");
    std::filesystem::create_directories(indexDirectory / "assets");
    expectInvalidRoot(indexDirectory);

    const auto assetsFile = directory_ / "assets-file";
    std::filesystem::create_directories(assetsFile);
    writeFile(assetsFile / "index.html", "valid index");
    writeFile(assetsFile / "assets", "not a directory");
    expectInvalidRoot(assetsFile);
}

TEST_F(WebApiStaticFilesTest, RejectsAssetPathTraversal) {
    writeFile(directory_ / "secret.txt", "outside-web-root");
    startServer(webRoot_);
    expectNotFound(get("/assets/../../secret.txt"));
    expectNotFound(get("/assets/%2e%2e/%2e%2e/secret.txt"));
}

TEST_F(WebApiStaticFilesTest, ServesAssetFromUnicodeWebRoot) {
    const auto unicodeRoot =
        directory_ / std::filesystem::path{u8"链接"};
    std::filesystem::rename(webRoot_, unicodeRoot);
    startServer(unicodeRoot);
    const auto response = get("/assets/app.js");

    ASSERT_TRUE(response);
    EXPECT_EQ(response->status, httplib::StatusCode::OK_200);
    EXPECT_EQ(response->body, "console.log('vna');");
}

TEST_F(WebApiStaticFilesTest, RejectsAssetLinkOutsideWebRoot) {
    const auto outside = directory_ / "outside";
    std::filesystem::create_directories(outside);
    writeFile(outside / "secret.txt", "outside-web-root");
    const auto error = createDirectoryLink(
        outside, webRoot_ / "assets" / "escape");
#ifdef _WIN32
    if (error.value() == ERROR_PRIVILEGE_NOT_HELD ||
        error.value() == ERROR_ACCESS_DENIED) {
        GTEST_SKIP() << error.message();
    }
#endif
    ASSERT_FALSE(error) << error.message();
    startServer(webRoot_);
    const auto response = get("/assets/escape/secret.txt");

    expectNotFound(response);
}

}  // namespace
}  // namespace vna::web_api
