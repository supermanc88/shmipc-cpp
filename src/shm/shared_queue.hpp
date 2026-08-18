#pragma once

#include "shm/queue_layout.hpp"

#include <cstddef>
#include <cstdint>
#include <mutex>
#include <vector>

namespace shmipc::shm {

enum class QueueError {
    none,
    null_memory,
    truncated_region,
    invalid_capacity,
    invalid_architecture,
    misaligned_atomic,
    invalid_state,
    full,
    empty,
};

template <typename T>
struct QueueResult {
    T value{};
    QueueError error{QueueError::none};

    [[nodiscard]] explicit operator bool() const noexcept {
        return error == QueueError::none;
    }
};

class SharedQueue final {
public:
    SharedQueue() noexcept = default;
    SharedQueue(const SharedQueue&) = delete;
    SharedQueue& operator=(const SharedQueue&) = delete;
    SharedQueue(SharedQueue&& other) noexcept;
    SharedQueue& operator=(SharedQueue&&) = delete;

    [[nodiscard]] explicit operator bool() const noexcept;
    [[nodiscard]] std::uint32_t capacity() const noexcept;
    [[nodiscard]] std::int64_t size() const noexcept;
    [[nodiscard]] bool empty() const noexcept;
    [[nodiscard]] bool full() const noexcept;

    [[nodiscard]] QueueError put(const QueueElement& element) noexcept;
    [[nodiscard]] QueueResult<QueueElement> pop() noexcept;
    [[nodiscard]] QueueResult<std::vector<QueueElement>> pop_batch(
        std::size_t maximum);

    [[nodiscard]] bool consumer_is_working() const noexcept;
    [[nodiscard]] bool mark_working() noexcept;
    [[nodiscard]] bool mark_not_working() noexcept;

private:
    friend QueueResult<SharedQueue> initialize_shared_queue(
        std::uint8_t*, std::size_t, std::uint32_t, QueueArchitecture);
    friend QueueResult<SharedQueue> map_shared_queue(
        std::uint8_t*, std::size_t, QueueArchitecture);

    SharedQueue(std::uint8_t* memory, std::size_t size,
                std::uint32_t capacity, QueueArchitecture architecture) noexcept;

    std::uint8_t* memory_{nullptr};
    std::size_t size_bytes_{0};
    std::uint32_t capacity_{0};
    QueueArchitecture architecture_{QueueArchitecture::amd64};
    std::mutex producer_mutex_{};
};

using SharedQueueResult = QueueResult<SharedQueue>;
using QueueElementResult = QueueResult<QueueElement>;
using QueueBatchResult = QueueResult<std::vector<QueueElement>>;

[[nodiscard]] constexpr QueueArchitecture native_queue_architecture() noexcept {
#if defined(__aarch64__) || defined(_M_ARM64)
    return QueueArchitecture::arm64;
#else
    return QueueArchitecture::amd64;
#endif
}

[[nodiscard]] const char* to_string(QueueError error) noexcept;
[[nodiscard]] SharedQueueResult initialize_shared_queue(
    std::uint8_t* memory, std::size_t size, std::uint32_t capacity,
    QueueArchitecture architecture = native_queue_architecture());
[[nodiscard]] SharedQueueResult map_shared_queue(
    std::uint8_t* memory, std::size_t size,
    QueueArchitecture architecture = native_queue_architecture());

}  // namespace shmipc::shm
