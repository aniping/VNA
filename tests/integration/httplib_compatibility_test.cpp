#include <gtest/gtest.h>

#include <httplib.h>

#include <atomic>
#include <chrono>
#include <functional>
#include <future>
#include <string>
#include <thread>

namespace {

using namespace std::chrono_literals;

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
        server_.WebSocket(
            "/close-now",
            [this](const httplib::Request&, httplib::ws::WebSocket& socket) {
                auto close = [this, &socket](std::promise<void>& ready) {
                    ready.set_value();
                    closeAllowed_.wait();
                    socket.close_now(
                        httplib::ws::CloseStatus::GoingAway,
                        "server stopping");
                    closeCallsReturned_.fetch_add(1);
                };
                std::thread firstCloser(close, std::ref(firstCloserReady_));
                std::thread secondCloser(close, std::ref(secondCloserReady_));
                socket.send("reader-ready");
                std::string ignored;
                readResult_.set_value(socket.read(ignored));
                firstCloser.join();
                secondCloser.join();
                closeHandlerReturned_.set_value();
            });
        server_.WebSocket(
            "/regular-close",
            [this](const httplib::Request&, httplib::ws::WebSocket& socket) {
                socket.close(httplib::ws::CloseStatus::Normal, "complete");
                regularCloseReturned_.set_value();
            });
        installBlockedSendRoute();

        port_ = server_.bind_to_any_port("127.0.0.1");
        ASSERT_GT(port_, 0);
        serverThread_ = std::thread([this]() {
            server_.listen_after_bind();
        });
        server_.wait_until_ready();
    }

    void installBlockedSendRoute() {
        server_.WebSocket(
            "/blocked-send",
            [this](const httplib::Request&, httplib::ws::WebSocket& socket) {
                const std::string payload(1024U * 1024U, 'x');
                std::thread closer{[this, &socket] {
                    blockedCloseAllowed_.wait();
                    socket.close_now(
                        httplib::ws::CloseStatus::GoingAway,
                        "server stopping");
                    blockedCloseReturned_.set_value();
                }};
                blockedSendStarted_.set_value();
                auto sent = true;
                while (sent) {
                    sent = socket.send(payload);
                }
                blockedSendReturned_.set_value(sent);
                closer.join();
            });
    }

    void TearDown() override {
        server_.stop();
        if (serverThread_.joinable()) {
            serverThread_.join();
        }
    }

    httplib::Server server_;
    std::promise<void> firstCloserReady_;
    std::promise<void> secondCloserReady_;
    std::promise<void> allowClose_;
    std::shared_future<void> closeAllowed_{allowClose_.get_future()};
    std::promise<void> closeHandlerReturned_;
    std::promise<void> regularCloseReturned_;
    std::promise<void> allowBlockedClose_;
    std::shared_future<void> blockedCloseAllowed_{
        allowBlockedClose_.get_future()};
    std::promise<void> blockedSendStarted_;
    std::promise<void> blockedCloseReturned_;
    std::promise<bool> blockedSendReturned_;
    std::promise<httplib::ws::ReadResult> readResult_;
    std::atomic<int> closeCallsReturned_{0};
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

TEST_F(HttplibCompatibilityTest, ServerCloseCompletesNormalHandshake) {
    auto returned = regularCloseReturned_.get_future();
    httplib::ws::WebSocketClient client{
        "ws://127.0.0.1:" + std::to_string(port_) + "/regular-close"};
    ASSERT_TRUE(client.connect());
    auto readResult = std::async(std::launch::async, [&client]() {
        std::string ignored;
        return client.read(ignored);
    });

    ASSERT_EQ(returned.wait_for(2s), std::future_status::ready);
    ASSERT_EQ(readResult.wait_for(2s), std::future_status::ready);
    EXPECT_EQ(readResult.get(), httplib::ws::ReadResult::Fail);
}

TEST_F(HttplibCompatibilityTest, ConcurrentImmediateCloseWakesReader) {
    auto firstReady = firstCloserReady_.get_future();
    auto secondReady = secondCloserReady_.get_future();
    auto returned = closeHandlerReturned_.get_future();
    auto readResult = readResult_.get_future();
    httplib::ws::WebSocketClient client{
        "ws://127.0.0.1:" + std::to_string(port_) + "/close-now"};
    client.set_read_timeout(2, 0);
    ASSERT_TRUE(client.connect());
    std::string readiness;
    const auto readinessResult = client.read(readiness);
    const auto firstStatus = firstReady.wait_for(2s);
    const auto secondStatus = secondReady.wait_for(2s);

    allowClose_.set_value();

    EXPECT_EQ(readinessResult, httplib::ws::ReadResult::Text);
    EXPECT_EQ(readiness, "reader-ready");
    ASSERT_EQ(firstStatus, std::future_status::ready);
    ASSERT_EQ(secondStatus, std::future_status::ready);
    ASSERT_EQ(returned.wait_for(2s), std::future_status::ready);
    ASSERT_EQ(readResult.wait_for(2s), std::future_status::ready);
    EXPECT_EQ(readResult.get(), httplib::ws::ReadResult::Fail);
    EXPECT_EQ(closeCallsReturned_.load(), 2);

    // Repeated new transports exercise descriptor reuse after the server-owned
    // close claim; a double close could otherwise terminate a later client.
    for (int request = 0; request < 64; ++request) {
        httplib::Client healthClient{"127.0.0.1", port_};
        const auto response = healthClient.Get("/health");
        ASSERT_TRUE(response);
        EXPECT_EQ(response->status, httplib::StatusCode::OK_200);
    }
}

TEST_F(HttplibCompatibilityTest, ImmediateCloseInterruptsBlockedWriter) {
    auto sendStarted = blockedSendStarted_.get_future();
    auto closeReturned = blockedCloseReturned_.get_future();
    auto sendReturned = blockedSendReturned_.get_future();
    httplib::ws::WebSocketClient client{
        "ws://127.0.0.1:" + std::to_string(port_) + "/blocked-send"};
    client.set_read_timeout(1, 0);
    client.set_socket_options([](socket_t socket) {
        const int bufferBytes = 1024;
        static_cast<void>(::setsockopt(
            socket, SOL_SOCKET, SO_RCVBUF,
            reinterpret_cast<const char*>(&bufferBytes),
            sizeof(bufferBytes)));
    });
    const auto connected = client.connect();
    const auto startedStatus = connected
        ? sendStarted.wait_for(2s)
        : std::future_status::timeout;

    const auto wasBlocked = startedStatus == std::future_status::ready &&
        sendReturned.wait_for(100ms) == std::future_status::timeout;
    allowBlockedClose_.set_value();

    EXPECT_TRUE(connected);
    EXPECT_EQ(startedStatus, std::future_status::ready);
    if (!connected) {
        return;
    }
    EXPECT_TRUE(wasBlocked);
    EXPECT_EQ(closeReturned.wait_for(2s), std::future_status::ready);
    EXPECT_EQ(sendReturned.wait_for(2s), std::future_status::ready);
    if (sendReturned.wait_for(0s) == std::future_status::ready) {
        EXPECT_FALSE(sendReturned.get());
    }
}

}  // namespace
