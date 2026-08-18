#include "shm/shared_memory_region.hpp"
#include "shm/shared_queue.hpp"

#include <atomic>
#include <cstdint>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

#include <sched.h>
#include <sys/wait.h>
#include <unistd.h>

namespace {

using shmipc::shm::QueueElement;
using shmipc::shm::QueueError;

bool test_basic_and_working_flag() {
    const auto bytes = shmipc::shm::queue_region_size(4U);
    std::vector<std::uint8_t> memory(bytes.value, 0);
    auto queue = shmipc::shm::initialize_shared_queue(
        memory.data(), memory.size(), 4U);
    if (!queue || queue.value.capacity() != 4U || !queue.value.empty() ||
        queue.value.full() || queue.value.consumer_is_working() ||
        !queue.value.mark_working() || queue.value.mark_working() ||
        !queue.value.consumer_is_working() ||
        !queue.value.mark_not_working()) {
        return false;
    }
    for (std::uint32_t index = 0; index < 4U; ++index) {
        if (queue.value.put({index, index + 10U, index + 20U}) !=
            QueueError::none) {
            return false;
        }
    }
    if (!queue.value.full() ||
        queue.value.put({}) != QueueError::full ||
        queue.value.mark_not_working() ||
        !queue.value.consumer_is_working()) {
        return false;
    }
    const auto first_batch = queue.value.pop_batch(3U);
    const auto second_batch = queue.value.pop_batch(3U);
    if (!first_batch || first_batch.value.size() != 3U || !second_batch ||
        second_batch.value.size() != 1U || !queue.value.empty() ||
        queue.value.pop().error != QueueError::empty ||
        !queue.value.mark_not_working()) {
        return false;
    }
    for (std::size_t index = 0; index < first_batch.value.size(); ++index) {
        if (first_batch.value[index].sequence_id != index ||
            first_batch.value[index].buffer_offset != index + 10U ||
            first_batch.value[index].status != index + 20U) {
            return false;
        }
    }
    return second_batch.value.front().sequence_id == 3U;
}

bool test_multi_producer_order() {
    constexpr std::uint32_t producers = 4U;
    constexpr std::uint32_t each = 5000U;
    constexpr std::uint32_t total = producers * each;
    constexpr std::uint32_t capacity = 256U;
    const auto bytes = shmipc::shm::queue_region_size(capacity);
    std::vector<std::uint8_t> memory(bytes.value, 0);
    auto queue = shmipc::shm::initialize_shared_queue(
        memory.data(), memory.size(), capacity);
    if (!queue) {
        return false;
    }
    std::vector<std::thread> threads;
    for (std::uint32_t producer = 0; producer < producers; ++producer) {
        threads.emplace_back([producer, &queue]() {
            for (std::uint32_t sequence = 0; sequence < each; ++sequence) {
                for (;;) {
                    const auto error = queue.value.put(
                        {producer, sequence, producer ^ sequence});
                    if (error == QueueError::none) {
                        break;
                    }
                    if (error != QueueError::full) {
                        std::terminate();
                    }
                    std::this_thread::yield();
                }
            }
        });
    }
    std::vector<std::uint32_t> next(producers, 0U);
    for (std::uint32_t consumed = 0; consumed < total;) {
        const auto result = queue.value.pop();
        if (!result) {
            if (result.error == QueueError::empty) {
                std::this_thread::yield();
                continue;
            }
            return false;
        }
        const auto& element = result.value;
        if (element.sequence_id >= producers ||
            element.buffer_offset != next[element.sequence_id] ||
            element.status != (element.sequence_id ^ element.buffer_offset)) {
            return false;
        }
        ++next[element.sequence_id];
        ++consumed;
    }
    for (auto& thread : threads) {
        thread.join();
    }
    for (const auto count : next) {
        if (count != each) {
            return false;
        }
    }
    return queue.value.empty();
}

bool test_working_flag_race() {
    const auto bytes = shmipc::shm::queue_region_size(2U);
    std::vector<std::uint8_t> memory(bytes.value, 0);
    auto queue = shmipc::shm::initialize_shared_queue(
        memory.data(), memory.size(), 2U);
    if (!queue) {
        return false;
    }
    for (std::uint32_t iteration = 0; iteration < 1000U; ++iteration) {
        if (!queue.value.mark_working()) {
            return false;
        }
        std::atomic<bool> start{false};
        bool notified = false;
        std::thread producer([&]() {
            while (!start.load(std::memory_order_acquire)) {
                std::this_thread::yield();
            }
            if (queue.value.put({iteration, 0, 0}) != QueueError::none) {
                std::terminate();
            }
            notified = queue.value.mark_working();
        });
        start.store(true, std::memory_order_release);
        const auto went_idle = queue.value.mark_not_working();
        producer.join();
        if ((!notified && went_idle) ||
            !queue.value.consumer_is_working() || !queue.value.pop()) {
            return false;
        }
        if (!queue.value.mark_not_working()) {
            return false;
        }
    }
    return true;
}

bool test_cross_process_wraparound() {
    constexpr std::uint32_t capacity = 256U;
    constexpr std::uint32_t count = 20000U;
    const auto bytes = shmipc::shm::queue_region_size(capacity);
    const auto path = std::string("/tmp/shmipc-cpp-queue-test-") +
                      std::to_string(static_cast<long long>(::getpid()));
    auto region = shmipc::shm::create_file_region(
        path, bytes.value,
        shmipc::shm::FileCleanup::unlink_on_destroy);
    if (!region) {
        return false;
    }
    auto queue = shmipc::shm::initialize_shared_queue(
        region.value.data(), region.value.size(), capacity);
    if (!queue) {
        return false;
    }
    const auto child = ::fork();
    if (child < 0) {
        return false;
    }
    if (child == 0) {
        auto consumer = shmipc::shm::map_shared_queue(
            region.value.data(), region.value.size());
        if (!consumer) {
            _exit(2);
        }
        for (std::uint32_t expected = 0; expected < count;) {
            const auto element = consumer.value.pop();
            if (!element) {
                if (element.error == QueueError::empty) {
                    ::sched_yield();
                    continue;
                }
                _exit(3);
            }
            if (element.value.sequence_id != expected ||
                element.value.buffer_offset != expected + 1U ||
                element.value.status != (expected ^ 0xa5a5U)) {
                _exit(4);
            }
            ++expected;
        }
        _exit(0);
    }
    for (std::uint32_t value = 0; value < count;) {
        const auto error = queue.value.put(
            {value, value + 1U, value ^ 0xa5a5U});
        if (error == QueueError::full) {
            ::sched_yield();
            continue;
        }
        if (error != QueueError::none) {
            return false;
        }
        ++value;
    }
    int status = 0;
    return ::waitpid(child, &status, 0) == child && WIFEXITED(status) &&
           WEXITSTATUS(status) == 0 && queue.value.empty();
}

bool test_errors() {
    std::vector<std::uint8_t> memory(128U, 0);
    if (shmipc::shm::initialize_shared_queue(nullptr, 0, 1U).error !=
            QueueError::null_memory ||
        shmipc::shm::initialize_shared_queue(memory.data(), memory.size(), 0U)
                .error != QueueError::invalid_capacity ||
        shmipc::shm::initialize_shared_queue(memory.data(), 24U, 1U).error !=
            QueueError::truncated_region ||
        shmipc::shm::initialize_shared_queue(memory.data() + 1U,
                                             memory.size() - 1U, 1U)
                .error != QueueError::misaligned_atomic) {
        return false;
    }
    const auto foreign = shmipc::shm::native_queue_architecture() ==
                                 shmipc::shm::QueueArchitecture::amd64
                             ? shmipc::shm::QueueArchitecture::arm64
                             : shmipc::shm::QueueArchitecture::amd64;
    if (shmipc::shm::initialize_shared_queue(memory.data(), memory.size(),
                                              1U, foreign)
            .error != QueueError::invalid_architecture) {
        return false;
    }
    auto valid = shmipc::shm::initialize_shared_queue(memory.data(),
                                                       memory.size(), 2U);
    const shmipc::shm::QueueHeader corrupt{2U, 2, 1, 0U};
    return valid &&
           shmipc::shm::write_queue_header(
               memory.data(), memory.size(),
               shmipc::shm::native_queue_architecture(), corrupt) ==
               shmipc::shm::LayoutError::none &&
           shmipc::shm::map_shared_queue(memory.data(), memory.size()).error ==
               QueueError::invalid_state;
}

}  // namespace

int main() {
    if (!test_basic_and_working_flag()) {
        std::cerr << "shared queue basic/working test failed\n";
        return 1;
    }
    if (!test_multi_producer_order()) {
        std::cerr << "shared queue MPSC test failed\n";
        return 1;
    }
    if (!test_cross_process_wraparound()) {
        std::cerr << "shared queue cross-process test failed\n";
        return 1;
    }
    if (!test_working_flag_race()) {
        std::cerr << "shared queue working race test failed\n";
        return 1;
    }
    if (!test_errors()) {
        std::cerr << "shared queue error test failed\n";
        return 1;
    }
    return 0;
}
