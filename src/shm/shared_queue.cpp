#include "shm/shared_queue.hpp"

#include "shm/atomic_word.hpp"

#include <algorithm>
#include <cstring>
#include <limits>
#include <utility>

namespace shmipc::shm {
namespace {

template <typename T>
T* word(std::uint8_t* memory, std::size_t offset) noexcept {
    return reinterpret_cast<T*>(memory + offset);
}

template <typename T>
const T* word(const std::uint8_t* memory, std::size_t offset) noexcept {
    return reinterpret_cast<const T*>(memory + offset);
}

bool valid_atomic_locations(const std::uint8_t* memory,
                            QueueArchitecture architecture) noexcept {
    const auto offsets = queue_offsets(architecture);
    if (!detail::atomic_word_aligned(word<std::uint32_t>(memory,
                                                         offsets.working))) {
        return false;
    }
    if (architecture == QueueArchitecture::arm64) {
        return detail::atomic_word_aligned(
                   word<std::int64_t>(memory, offsets.head)) &&
               detail::atomic_word_aligned(
                   word<std::int64_t>(memory, offsets.tail));
    }
    return true;
}

QueueError validate_state(std::int64_t head, std::int64_t tail,
                          std::uint32_t capacity) noexcept {
    if (head < 0 || tail < head ||
        static_cast<std::uint64_t>(tail - head) > capacity) {
        return QueueError::invalid_state;
    }
    return QueueError::none;
}

}  // namespace

SharedQueue::SharedQueue(std::uint8_t* memory, std::size_t size,
                         std::uint32_t capacity,
                         QueueArchitecture architecture) noexcept
    : memory_(memory),
      size_bytes_(size),
      capacity_(capacity),
      architecture_(architecture) {}

SharedQueue::SharedQueue(SharedQueue&& other) noexcept
    : memory_(other.memory_),
      size_bytes_(other.size_bytes_),
      capacity_(other.capacity_),
      architecture_(other.architecture_) {
    other.memory_ = nullptr;
    other.size_bytes_ = 0;
    other.capacity_ = 0;
}

SharedQueue::operator bool() const noexcept {
    return memory_ != nullptr && capacity_ != 0U;
}

std::uint32_t SharedQueue::capacity() const noexcept { return capacity_; }

std::int64_t SharedQueue::size() const noexcept {
    const auto offsets = queue_offsets(architecture_);
    const auto tail = detail::atomic_load(
        word<std::int64_t>(memory_, offsets.tail));
    const auto head = detail::atomic_load(
        word<std::int64_t>(memory_, offsets.head));
    return tail - head;
}

bool SharedQueue::empty() const noexcept { return size() == 0; }

bool SharedQueue::full() const noexcept {
    return size() == static_cast<std::int64_t>(capacity_);
}

QueueError SharedQueue::put(const QueueElement& element) noexcept {
    std::lock_guard<std::mutex> lock(producer_mutex_);
    const auto offsets = queue_offsets(architecture_);
    auto* const tail_word = word<std::int64_t>(memory_, offsets.tail);
    const auto tail = detail::atomic_load(tail_word);
    const auto head = detail::atomic_load(
        word<std::int64_t>(memory_, offsets.head));
    const auto state = validate_state(head, tail, capacity_);
    if (state != QueueError::none) {
        return state;
    }
    if (tail - head == static_cast<std::int64_t>(capacity_)) {
        return QueueError::full;
    }
    const auto slot = static_cast<std::uint32_t>(
        static_cast<std::uint64_t>(tail) % capacity_);
    if (write_queue_element(memory_, size_bytes_, slot, element) !=
        LayoutError::none) {
        return QueueError::invalid_state;
    }
    static_cast<void>(detail::atomic_fetch_add(tail_word,
                                               static_cast<std::int64_t>(1)));
    return QueueError::none;
}

QueueElementResult SharedQueue::pop() noexcept {
    const auto offsets = queue_offsets(architecture_);
    auto* const head_word = word<std::int64_t>(memory_, offsets.head);
    const auto head = detail::atomic_load(head_word);
    const auto tail = detail::atomic_load(
        word<std::int64_t>(memory_, offsets.tail));
    const auto state = validate_state(head, tail, capacity_);
    if (state != QueueError::none) {
        return {{}, state};
    }
    if (head == tail) {
        return {{}, QueueError::empty};
    }
    const auto slot = static_cast<std::uint32_t>(
        static_cast<std::uint64_t>(head) % capacity_);
    const auto element = read_queue_element(memory_, size_bytes_, slot);
    if (!element) {
        return {{}, QueueError::invalid_state};
    }
    static_cast<void>(detail::atomic_fetch_add(head_word,
                                               static_cast<std::int64_t>(1)));
    return {element.value, QueueError::none};
}

QueueBatchResult SharedQueue::pop_batch(std::size_t maximum) {
    std::vector<QueueElement> elements;
    elements.reserve(std::min<std::size_t>(maximum, capacity_));
    while (elements.size() < maximum) {
        const auto element = pop();
        if (!element) {
            if (element.error == QueueError::empty) {
                break;
            }
            return {{}, element.error};
        }
        elements.push_back(element.value);
    }
    return {std::move(elements), QueueError::none};
}

bool SharedQueue::consumer_is_working() const noexcept {
    const auto offset = queue_offsets(architecture_).working;
    return detail::atomic_load(word<std::uint32_t>(memory_, offset)) > 0U;
}

bool SharedQueue::mark_working() noexcept {
    const auto offset = queue_offsets(architecture_).working;
    auto* const working = word<std::uint32_t>(memory_, offset);
    std::uint32_t expected = 0;
    return detail::atomic_compare_exchange(working, expected, 1U);
}

bool SharedQueue::mark_not_working() noexcept {
    const auto offset = queue_offsets(architecture_).working;
    auto* const working = word<std::uint32_t>(memory_, offset);
    detail::atomic_store(working, 0U);
    if (empty()) {
        return true;
    }
    detail::atomic_store(working, 1U);
    return false;
}

const char* to_string(QueueError error) noexcept {
    switch (error) {
        case QueueError::none:
            return "none";
        case QueueError::null_memory:
            return "null memory";
        case QueueError::truncated_region:
            return "truncated region";
        case QueueError::invalid_capacity:
            return "invalid capacity";
        case QueueError::invalid_architecture:
            return "invalid architecture";
        case QueueError::misaligned_atomic:
            return "misaligned atomic word";
        case QueueError::invalid_state:
            return "invalid queue state";
        case QueueError::full:
            return "queue full";
        case QueueError::empty:
            return "queue empty";
    }
    return "unknown queue error";
}

SharedQueueResult initialize_shared_queue(std::uint8_t* memory,
                                          std::size_t size,
                                          std::uint32_t capacity,
                                          QueueArchitecture architecture) {
    if (memory == nullptr) {
        return {{}, QueueError::null_memory};
    }
    if (architecture != native_queue_architecture()) {
        return {{}, QueueError::invalid_architecture};
    }
    const auto expected = queue_region_size(capacity);
    if (!expected) {
        return {{}, QueueError::invalid_capacity};
    }
    if (size < expected.value) {
        return {{}, QueueError::truncated_region};
    }
    if (!valid_atomic_locations(memory, architecture)) {
        return {{}, QueueError::misaligned_atomic};
    }
    std::memset(memory, 0, expected.value);
    std::memcpy(memory, &capacity, sizeof(capacity));
    const auto offsets = queue_offsets(architecture);
    detail::atomic_store(word<std::int64_t>(memory, offsets.head),
                         static_cast<std::int64_t>(0));
    detail::atomic_store(word<std::int64_t>(memory, offsets.tail),
                         static_cast<std::int64_t>(0));
    detail::atomic_store(word<std::uint32_t>(memory, offsets.working), 0U);
    return {SharedQueue(memory, expected.value, capacity, architecture),
            QueueError::none};
}

SharedQueueResult map_shared_queue(std::uint8_t* memory, std::size_t size,
                                   QueueArchitecture architecture) {
    if (memory == nullptr) {
        return {{}, QueueError::null_memory};
    }
    if (architecture != native_queue_architecture()) {
        return {{}, QueueError::invalid_architecture};
    }
    if (size < queue_header_size) {
        return {{}, QueueError::truncated_region};
    }
    std::uint32_t capacity = 0;
    std::memcpy(&capacity, memory, sizeof(capacity));
    const auto expected = queue_region_size(capacity);
    if (!expected) {
        return {{}, QueueError::invalid_capacity};
    }
    if (size < expected.value) {
        return {{}, QueueError::truncated_region};
    }
    if (!valid_atomic_locations(memory, architecture)) {
        return {{}, QueueError::misaligned_atomic};
    }
    const auto offsets = queue_offsets(architecture);
    const auto head = detail::atomic_load(
        word<std::int64_t>(memory, offsets.head));
    const auto tail = detail::atomic_load(
        word<std::int64_t>(memory, offsets.tail));
    if (validate_state(head, tail, capacity) != QueueError::none) {
        return {{}, QueueError::invalid_state};
    }
    return {SharedQueue(memory, expected.value, capacity, architecture),
            QueueError::none};
}

}  // namespace shmipc::shm
