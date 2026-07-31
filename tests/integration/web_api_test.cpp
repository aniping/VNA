#include <gtest/gtest.h>

#include <httplib.h>

#include <thread>

#include <vna/web_api/web_api.hpp>

namespace vna::web_api {
namespace {

class WebApiTest : public ::testing::Test {
protected:
    void SetUp() override {
        webApi_.install(server_);
        port_ = server_.bind_to_any_port("127.0.0.1");
        ASSERT_GT(port_, 0);
        serverThread_ = std::thread([this] { server_.listen_after_bind(); });
        server_.wait_until_ready();
    }

    void TearDown() override {
        server_.stop();
        if (serverThread_.joinable()) {
            serverThread_.join();
        }
    }

    application::CommandBus commandBus_{
        application::InstrumentId{"instrument-1"}};
    WebApi webApi_{commandBus_};
    httplib::Server server_;
    int port_{-1};
    std::thread serverThread_;
};

TEST_F(WebApiTest, ReportsHealthOverHttp) {
    httplib::Client client{"127.0.0.1", port_};

    const auto response = client.Get("/api/v1/health");

    ASSERT_TRUE(response);
    EXPECT_EQ(response->status, httplib::StatusCode::OK_200);
    EXPECT_EQ(response->get_header_value("Content-Type"), "application/json");
    EXPECT_EQ(response->body, R"({"status":"ok"})");
}

}  // namespace
}  // namespace vna::web_api
