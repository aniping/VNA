#pragma once

#include <algorithm>
#include <condition_variable>
#include <cstdlib>
#include <memory>
#include <mutex>
#include <thread>
#include <utility>
#include <vector>

namespace vna::compat {
namespace detail {

struct StopCallbackRegistration {
    std::mutex mutex;
    std::condition_variable finished;
    bool active{true};
    bool executing{};
    std::thread::id executor;

    virtual ~StopCallbackRegistration() = default;
    virtual void invoke() noexcept = 0;
};

struct StopState {
    std::mutex mutex;
    bool requested{};
    std::vector<std::shared_ptr<StopCallbackRegistration>> callbacks;
};

inline void invokeStopCallback(const std::shared_ptr<StopCallbackRegistration>& registration) noexcept {
    {
        std::lock_guard lock{registration->mutex};
        if (!registration->active) {
            return;
        }
        registration->executing = true;
        registration->executor = std::this_thread::get_id();
    }
    registration->invoke();
    {
        std::lock_guard lock{registration->mutex};
        registration->active = false;
        registration->executing = false;
    }
    registration->finished.notify_all();
}

}  // namespace detail

class StopSource;

class StopToken {
public:
    StopToken() noexcept = default;

    [[nodiscard]] bool stopRequested() const noexcept {
        if (!state_) {
            return false;
        }
        std::lock_guard lock{state_->mutex};
        return state_->requested;
    }

private:
    explicit StopToken(std::shared_ptr<detail::StopState> state) noexcept
        : state_{std::move(state)} {}

    std::shared_ptr<detail::StopState> state_;

    friend class StopSource;
    template <typename Callback>
    friend class StopCallback;
};

class StopSource {
public:
    StopSource() : state_{std::make_shared<detail::StopState>()} {}

    [[nodiscard]] StopToken getToken() const noexcept {
        return StopToken{state_};
    }

    bool requestStop() noexcept {
        if (!state_) {
            return false;
        }
        std::vector<std::shared_ptr<detail::StopCallbackRegistration>> pending;
        {
            std::lock_guard lock{state_->mutex};
            if (state_->requested) {
                return false;
            }
            state_->requested = true;
            pending.swap(state_->callbacks);
        }
        for (const auto& registration : pending) {
            detail::invokeStopCallback(registration);
        }
        return true;
    }

private:
    std::shared_ptr<detail::StopState> state_;
};

template <typename Callback>
class StopCallback {
private:
    class Registration final : public detail::StopCallbackRegistration {
    public:
        explicit Registration(Callback callback)
            : callback_{std::move(callback)} {}

        void invoke() noexcept override {
            try {
                callback_();
            } catch (...) {
                std::terminate();
            }
        }

    private:
        Callback callback_;
    };

public:
    StopCallback(StopToken token, Callback callback) {
        if (!token.state_) {
            return;
        }
        state_ = std::move(token.state_);
        registration_ = std::make_shared<Registration>(std::move(callback));
        bool invokeImmediately{};
        {
            std::lock_guard lock{state_->mutex};
            invokeImmediately = state_->requested;
            if (!invokeImmediately) {
                state_->callbacks.push_back(registration_);
            }
        }
        if (invokeImmediately) {
            detail::invokeStopCallback(registration_);
        }
    }

    StopCallback(const StopCallback&) = delete;
    StopCallback& operator=(const StopCallback&) = delete;
    ~StopCallback() { unregisterAndWait(); }

private:
    void unregisterAndWait() noexcept {
        if (!registration_) {
            return;
        }
        {
            std::lock_guard lock{state_->mutex};
            auto& callbacks = state_->callbacks;
            callbacks.erase(
                std::remove(callbacks.begin(), callbacks.end(), registration_),
                callbacks.end());
        }
        std::unique_lock lock{registration_->mutex};
        registration_->active = false;
        if (registration_->executor == std::this_thread::get_id()) {
            return;
        }
        registration_->finished.wait(
            lock, [this] { return !registration_->executing; });
    }

    std::shared_ptr<detail::StopState> state_;
    std::shared_ptr<detail::StopCallbackRegistration> registration_;
};

template <typename Callback>
StopCallback(StopToken, Callback) -> StopCallback<Callback>;

}  // namespace vna::compat
