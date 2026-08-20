#include "shmipc/session.hpp"

#include "public/session_impl.hpp"

#include <condition_variable>
#include <deque>
#include <stdexcept>
#include <thread>
#include <utility>

namespace shmipc {
namespace {

thread_local bool callback_executor_thread = false;

Status make_status(Error error) noexcept {
    return {error, 0};
}

Status map_receive_status(const core::V2SessionStatus& status) noexcept {
    switch (status.error) {
        case core::V2SessionError::none:
            return {};
        case core::V2SessionError::invalid_argument:
            return make_status(Error::invalid_argument);
        case core::V2SessionError::handshake_error:
            return make_status(Error::handshake_error);
        case core::V2SessionError::dispatcher_error:
            return make_status(Error::event_loop_error);
        case core::V2SessionError::transport_error:
            return make_status(Error::transport_error);
        case core::V2SessionError::codec_error:
        case core::V2SessionError::unexpected_event:
        case core::V2SessionError::unexpected_stream:
            return make_status(Error::protocol_error);
        case core::V2SessionError::queue_error:
        case core::V2SessionError::buffer_pool_error:
        case core::V2SessionError::buffer_io_error:
            return make_status(Error::shared_memory_error);
        case core::V2SessionError::unhealthy:
            return make_status(Error::unhealthy);
        case core::V2SessionError::closed:
            return make_status(Error::closed);
        case core::V2SessionError::timeout:
            return make_status(Error::timeout);
    }
    return make_status(Error::protocol_error);
}

}  // namespace

bool is_callback_executor_thread() noexcept {
    return callback_executor_thread;
}

struct CallbackExecutor::Impl final {
    struct State final {
        std::mutex mutex{};
        std::condition_variable condition{};
        std::deque<std::function<void()>> tasks{};
        bool stopping{false};
    };

    explicit Impl(std::size_t count) : state(std::make_shared<State>()) {
        threads.reserve(count);
        try {
            for (std::size_t index = 0; index < count; ++index) {
                threads.emplace_back([shared_state = state] {
                    callback_executor_thread = true;
                    for (;;) {
                        std::function<void()> task;
                        {
                            std::unique_lock<std::mutex> lock(shared_state->mutex);
                            shared_state->condition.wait(lock, [&] {
                                return shared_state->stopping ||
                                       !shared_state->tasks.empty();
                            });
                            if (shared_state->tasks.empty()) {
                                if (shared_state->stopping) {
                                    break;
                                }
                                continue;
                            }
                            task = std::move(shared_state->tasks.front());
                            shared_state->tasks.pop_front();
                        }
                        try {
                            task();
                        } catch (...) {
                        }
                    }
                    callback_executor_thread = false;
                });
            }
        } catch (...) {
            stop_and_join();
            throw;
        }
    }

    ~Impl() {
        stop_and_join();
    }

    void stop_and_join() noexcept {
        {
            std::lock_guard<std::mutex> lock(state->mutex);
            state->stopping = true;
        }
        state->condition.notify_all();
        const auto current = std::this_thread::get_id();
        for (auto& thread : threads) {
            if (!thread.joinable()) {
                continue;
            }
            if (thread.get_id() == current) {
                thread.detach();
            } else {
                thread.join();
            }
        }
    }

    [[nodiscard]] bool execute(std::function<void()> task) {
        {
            std::lock_guard<std::mutex> lock(state->mutex);
            if (state->stopping) {
                return false;
            }
            state->tasks.push_back(std::move(task));
        }
        state->condition.notify_one();
        return true;
    }

    std::shared_ptr<State> state{};
    std::vector<std::thread> threads{};
};

CallbackExecutor::CallbackExecutor(std::size_t thread_count) {
    if (thread_count == 0U) {
        throw std::invalid_argument("CallbackExecutor needs at least one thread");
    }
    impl_ = std::make_unique<Impl>(thread_count);
}

CallbackExecutor::~CallbackExecutor() = default;

std::size_t CallbackExecutor::thread_count() const noexcept {
    return impl_ == nullptr ? 0U : impl_->threads.size();
}

bool CallbackExecutor::execute(std::function<void()> task) {
    return impl_ != nullptr && impl_->execute(std::move(task));
}

void StreamCallbacks::on_local_close(Stream&) {}
void StreamCallbacks::on_remote_close(Stream&) {}
void StreamCallbacks::on_error(Stream&, const Status&) {}

struct AsyncCallbackState final
    : AsyncCallbackControl,
      std::enable_shared_from_this<AsyncCallbackState> {
    AsyncCallbackState(std::shared_ptr<Stream::Impl> stream_impl,
                       std::shared_ptr<StreamCallbacks> stream_callbacks,
                       std::shared_ptr<CallbackExecutor> callback_executor)
        : impl(std::move(stream_impl)),
          callbacks(std::move(stream_callbacks)),
          executor(std::move(callback_executor)) {}

    [[nodiscard]] bool start() {
        const auto weak = weak_from_this();
        {
            std::lock_guard<std::mutex> lock(mutex);
            token = impl->stream.set_readable_callback([weak] {
                if (const auto state = weak.lock()) {
                    state->notify();
                }
            });
            if (token == 0U) {
                stopping = true;
                finished = true;
                return false;
            }
        }
        notify();
        return true;
    }

    void notify() noexcept {
        bool schedule = false;
        {
            std::lock_guard<std::mutex> lock(mutex);
            if (stopping || finished) {
                return;
            }
            ++generation;
            if (!scheduled) {
                scheduled = true;
                schedule = true;
            }
        }
        if (!schedule) {
            return;
        }
        const auto self = shared_from_this();
        try {
            if (executor->execute([self] { self->pump(); })) {
                return;
            }
        } catch (...) {
        }
        finish();
    }

    void request_stop() noexcept {
        bool finish_now = false;
        {
            std::lock_guard<std::mutex> lock(mutex);
            if (finished) {
                return;
            }
            stopping = true;
            finish_now = !scheduled;
        }
        if (finish_now) {
            finish();
        }
    }

    void wait_until_finished() noexcept override {
        std::unique_lock<std::mutex> lock(mutex);
        condition.wait(lock, [&] { return finished; });
    }

    [[nodiscard]] bool is_active() const noexcept {
        std::lock_guard<std::mutex> lock(mutex);
        return !finished;
    }

    void pump() noexcept {
        for (;;) {
            std::uint64_t observed = 0U;
            {
                std::lock_guard<std::mutex> lock(mutex);
                if (stopping || finished) {
                    break;
                }
                observed = generation;
            }

            auto result = impl->stream.receive(std::chrono::milliseconds(0));
            if (result) {
                Stream view(impl);
                try {
                    callbacks->on_data(view, std::move(result.value));
                } catch (...) {
                    report_error(make_status(Error::callback_error));
                    impl->local_close_requested.store(true,
                                                       std::memory_order_release);
                    impl->closed.store(true, std::memory_order_release);
                    static_cast<void>(impl->stream.close());
                    report_terminal(true);
                    finish();
                    return;
                }
                if (impl->local_close_requested.load(std::memory_order_acquire)) {
                    report_terminal(true);
                    finish();
                    return;
                }
                continue;
            }

            const auto status = map_receive_status(result.status);
            if (status.error == Error::timeout) {
                std::lock_guard<std::mutex> lock(mutex);
                if (stopping || finished) {
                    break;
                }
                if (generation != observed) {
                    continue;
                }
                scheduled = false;
                return;
            }
            if (status.error == Error::closed) {
                const auto local = impl->local_close_requested.load(
                    std::memory_order_acquire);
                impl->closed.store(true, std::memory_order_release);
                report_terminal(local);
            } else {
                report_error(status);
            }
            finish();
            return;
        }
        finish();
    }

    void report_error(const Status& status) noexcept {
        Stream view(impl);
        try {
            callbacks->on_error(view, status);
        } catch (...) {
        }
    }

    void report_terminal(bool local) noexcept {
        bool report = false;
        {
            std::lock_guard<std::mutex> lock(mutex);
            if (!terminal_reported) {
                terminal_reported = true;
                report = true;
            }
        }
        if (!report) {
            return;
        }
        Stream view(impl);
        try {
            if (local) {
                callbacks->on_local_close(view);
            } else {
                callbacks->on_remote_close(view);
            }
        } catch (...) {
        }
    }

    void finish() noexcept {
        std::uint64_t callback_token = 0U;
        {
            std::lock_guard<std::mutex> lock(mutex);
            if (finished) {
                return;
            }
            stopping = true;
            finished = true;
            scheduled = false;
            callback_token = token;
            token = 0U;
        }
        impl->stream.clear_readable_callback(callback_token);
        {
            std::lock_guard<std::mutex> lock(impl->callback_mutex);
            const auto current = impl->callback_control.lock();
            if (current.get() == this) {
                impl->callback_control.reset();
            }
        }
        condition.notify_all();
    }

    std::shared_ptr<Stream::Impl> impl{};
    std::shared_ptr<StreamCallbacks> callbacks{};
    std::shared_ptr<CallbackExecutor> executor{};
    mutable std::mutex mutex{};
    std::condition_variable condition{};
    std::uint64_t generation{0U};
    std::uint64_t token{0U};
    bool scheduled{false};
    bool stopping{false};
    bool finished{false};
    bool terminal_reported{false};
};

struct CallbackSubscription::Impl final {
    explicit Impl(std::shared_ptr<AsyncCallbackState> callback_state)
        : state(std::move(callback_state)) {}

    std::shared_ptr<AsyncCallbackState> state{};
};

CallbackSubscription::CallbackSubscription() noexcept = default;
CallbackSubscription::~CallbackSubscription() {
    static_cast<void>(stop());
}
CallbackSubscription::CallbackSubscription(
    std::unique_ptr<Impl> impl) noexcept
    : impl_(std::move(impl)) {}
CallbackSubscription::CallbackSubscription(CallbackSubscription&&) noexcept =
    default;
CallbackSubscription& CallbackSubscription::operator=(
    CallbackSubscription&& other) noexcept {
    if (this != &other) {
        static_cast<void>(stop());
        impl_ = std::move(other.impl_);
    }
    return *this;
}

CallbackSubscription::operator bool() const noexcept {
    return impl_ != nullptr && impl_->state != nullptr &&
           impl_->state->is_active();
}

Status CallbackSubscription::stop() noexcept {
    if (impl_ == nullptr) {
        return {};
    }
    auto state = std::move(impl_->state);
    impl_.reset();
    if (state != nullptr) {
        state->request_stop();
        if (!is_callback_executor_thread()) {
            state->wait_until_finished();
        }
    }
    return {};
}

CallbackSubscriptionResult Stream::set_callbacks(
    std::shared_ptr<StreamCallbacks> callbacks,
    std::shared_ptr<CallbackExecutor> executor) {
    if (impl_ == nullptr || callbacks == nullptr || executor == nullptr) {
        return {{}, make_status(Error::invalid_argument)};
    }
    auto state = std::make_shared<AsyncCallbackState>(impl_,
                                                       std::move(callbacks),
                                                       std::move(executor));
    {
        std::lock_guard<std::mutex> lock(impl_->callback_mutex);
        if (impl_->closed.load(std::memory_order_acquire)) {
            return {{}, make_status(Error::closed)};
        }
        if (!impl_->callback_control.expired()) {
            return {{}, make_status(Error::callback_already_set)};
        }
        impl_->callback_control = state;
    }
    if (!state->start()) {
        {
            std::lock_guard<std::mutex> lock(impl_->callback_mutex);
            impl_->callback_control.reset();
        }
        return {{}, make_status(Error::closed)};
    }
    return {CallbackSubscription(
                std::make_unique<CallbackSubscription::Impl>(state)),
            {}};
}

}  // namespace shmipc
