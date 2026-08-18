#pragma once

#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace shmipc::shm::detail {

static_assert(__atomic_always_lock_free(sizeof(std::uint32_t), nullptr),
              "cross-process shared words require lock-free 32-bit atomics");
static_assert(__atomic_always_lock_free(sizeof(std::uint64_t), nullptr),
              "cross-process queues require lock-free 64-bit atomics");

template <typename T>
[[nodiscard]] bool atomic_word_aligned(const T* word) noexcept {
    static_assert(sizeof(T) == sizeof(std::uint32_t) ||
                  sizeof(T) == sizeof(std::uint64_t));
    return reinterpret_cast<std::uintptr_t>(word) % alignof(T) == 0U;
}

template <typename T>
[[nodiscard]] T atomic_load(const T* word) noexcept {
    static_assert(sizeof(T) == sizeof(std::uint32_t) ||
                  sizeof(T) == sizeof(std::uint64_t));
    static_assert(std::is_integral_v<T>);
    return __atomic_load_n(word, __ATOMIC_SEQ_CST);
}

template <typename T>
void atomic_store(T* word, T value) noexcept {
    static_assert(sizeof(T) == sizeof(std::uint32_t) ||
                  sizeof(T) == sizeof(std::uint64_t));
    static_assert(std::is_integral_v<T>);
    __atomic_store_n(word, value, __ATOMIC_SEQ_CST);
}

template <typename T>
[[nodiscard]] T atomic_fetch_add(T* word, T value) noexcept {
    static_assert(sizeof(T) == sizeof(std::uint32_t) ||
                  sizeof(T) == sizeof(std::uint64_t));
    static_assert(std::is_integral_v<T>);
    return __atomic_fetch_add(word, value, __ATOMIC_SEQ_CST);
}

template <typename T>
[[nodiscard]] bool atomic_compare_exchange(T* word, T& expected,
                                           T desired) noexcept {
    static_assert(sizeof(T) == sizeof(std::uint32_t) ||
                  sizeof(T) == sizeof(std::uint64_t));
    static_assert(std::is_integral_v<T>);
    return __atomic_compare_exchange_n(word, &expected, desired, false,
                                       __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
}

}  // namespace shmipc::shm::detail
