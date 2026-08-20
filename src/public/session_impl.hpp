#pragma once

#include "shmipc/session.hpp"

#include "core/v2_multiplexed_session.hpp"

#include <atomic>
#include <memory>
#include <mutex>

namespace shmipc {

struct AsyncCallbackControl {
    virtual ~AsyncCallbackControl() = default;
    virtual void wait_until_finished() noexcept = 0;
};

[[nodiscard]] bool is_callback_executor_thread() noexcept;

struct Stream::Impl final {
    explicit Impl(core::V2Stream&& value) noexcept : stream(std::move(value)) {}

    core::V2Stream stream{};
    std::mutex callback_mutex{};
    std::weak_ptr<AsyncCallbackControl> callback_control{};
    std::atomic<bool> local_close_requested{false};
    std::atomic<bool> closed{false};
};

}  // namespace shmipc
