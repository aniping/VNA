#include <gtest/gtest.h>

#include <httplib.h>

#include <string>
#include <thread>

namespace {

class HttplibCompatibilityTest : public ::testing::Test {
protected:
    void SetUp() override {
        server_.Get("/health", [](const httplib::Request&, httplib::Response& response) {
            response.set_content("ok", "text/plain");
        });
        server_.WebSocket(
            "/frames",
            [](const httplib::Request&, httplib::ws::WebSocket& socket) {
                std::string message;
                while (socket.read(message)) {
                    socket.send(message);
                }
            });

        port_ = server_.bind_to_any_port("127.0.0.1");
        ASSERT_GT(port_, 0);
        serverThread_ = std::thread([this]() {
            server_.listen_after_bind();
        });
        server_.wait_until_ready();
    }

    void TearDown() override {
        server_.stop();
        if (serverThread_.joinable()) {
            serverThread_.join();
        }
    }

    httplib::Server server_;
    int port_{-1};
    std::thread serverThread_;
};

TEST_F(HttplibCompatibilityTest, HttpRequestCompletesOnCurrentToolchain) {
    httplib::Client client{"127.0.0.1", port_};

    const auto response = client.Get("/health");

    ASSERT_TRUE(response);
    EXPECT_EQ(response->status, httplib::StatusCode::OK_200);
    EXPECT_EQ(response->body, "ok");
}

TEST_F(HttplibCompatibilityTest, WebSocketRoundTripCompletesOnCurrentToolchain) {
    httplib::ws::WebSocketClient client{
        "ws://127.0.0.1:" + std::to_string(port_) + "/frames"};
    ASSERT_TRUE(client.connect());

    ASSERT_TRUE(client.send("frame-1"));
    std::string response;
    ASSERT_TRUE(client.read(response));

    EXPECT_EQ(response, "frame-1");
    client.close();
}

}  // namespace
