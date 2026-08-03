#pragma once

#include <functional>
#include <thread>
#include <type_traits>
#include <utility>

#include <vna/compat/stop_token.hpp>

namespace vna::compat {

class JoiningThread {
public:
    JoiningThread() = default;

    template <
        typename Callable,
        typename... Args,
        std::enable_if_t<
            !std::is_same_v<std::decay_t<Callable>, JoiningThread>, int> = 0>
    explicit JoiningThread(Callable&& callable, Args&&... args)
        : stopSource_{},
          thread_{
              &JoiningThread::run<
                  std::decay_t<Callable>, std::decay_t<Args>...>,
              stopSource_.getToken(),
              std::forward<Callable>(callable),
              std::forward<Args>(args)...} {}

    JoiningThread(const JoiningThread&) = delete;
    JoiningThread& operator=(const JoiningThread&) = delete;

    JoiningThread(JoiningThread&&) noexcept = default;

    JoiningThread& operator=(JoiningThread&& other) noexcept {
        stopAndJoin();
        stopSource_ = std::move(other.stopSource_);
        thread_ = std::move(other.thread_);
        return *this;
    }

    ~JoiningThread() { stopAndJoin(); }

    [[nodiscard]] bool joinable() const noexcept { return thread_.joinable(); }
    bool requestStop() noexcept { return stopSource_.requestStop(); }

    void join() { thread_.join(); }

private:
    template <typename Callable, typename... Args>
    static void run(StopToken token, Callable callable, Args... args) {
        if constexpr (std::is_invocable_v<Callable&, StopToken, Args...>) {
            std::invoke(callable, std::move(token), std::move(args)...);
        } else {
            std::invoke(callable, std::move(args)...);
        }
    }

    void stopAndJoin() noexcept {
        if (!thread_.joinable()) {
            return;
        }
        static_cast<void>(stopSource_.requestStop());
        try {
            thread_.join();
        } catch (...) {
            std::terminate();
        }
    }

    StopSource stopSource_;
    std::thread thread_;
};

}  // namespace vna::compat
