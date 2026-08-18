#pragma once

#include "shm/buffer_pool.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace shmipc::shm {

enum class BufferIoError {
    none,
    null_data,
    invalid_size,
    no_buffer,
    invalid_state,
    out_of_range,
    pool_error,
};

template <typename T>
struct BufferIoResult {
    T value{};
    BufferIoError error{BufferIoError::none};

    [[nodiscard]] explicit operator bool() const noexcept {
        return error == BufferIoError::none;
    }
};

struct MutableBufferView {
    std::uint8_t* data{nullptr};
    std::size_t size{0};
};

class BufferReadView final {
public:
    BufferReadView() noexcept = default;

    [[nodiscard]] const std::uint8_t* data() const noexcept;
    [[nodiscard]] std::size_t size() const noexcept;
    [[nodiscard]] bool is_zero_copy() const noexcept;

private:
    friend class BufferReader;

    BufferReadView(const std::uint8_t* data, std::size_t size) noexcept;
    explicit BufferReadView(std::vector<std::uint8_t> owned) noexcept;

    const std::uint8_t* borrowed_{nullptr};
    std::size_t size_{0};
    std::vector<std::uint8_t> owned_{};
};

// The referenced pool and its mapping must outlive this writer.
class BufferWriter final {
public:
    explicit BufferWriter(BufferPool& pool) noexcept;
    ~BufferWriter();

    BufferWriter(const BufferWriter&) = delete;
    BufferWriter& operator=(const BufferWriter&) = delete;
    BufferWriter(BufferWriter&&) = delete;
    BufferWriter& operator=(BufferWriter&&) = delete;

    [[nodiscard]] std::uint64_t size() const noexcept;
    [[nodiscard]] std::size_t slice_count() const noexcept;
    [[nodiscard]] BufferIoError write_byte(std::uint8_t value);
    [[nodiscard]] BufferIoResult<std::size_t> write_bytes(
        const std::uint8_t* data, std::size_t size);
    [[nodiscard]] BufferIoResult<std::size_t> write_string(
        const std::string& value);
    [[nodiscard]] BufferIoResult<MutableBufferView> reserve(std::size_t size);
    [[nodiscard]] PublishedBufferChainResult publish() noexcept;

private:
    [[nodiscard]] BufferIoError append_allocation(std::uint32_t minimum);
    [[nodiscard]] BufferIoError append_chain(std::uint64_t minimum);
    void reset() noexcept;

    BufferPool* pool_{nullptr};
    BufferChain chain_{};
    std::vector<std::uint32_t> used_{};
    std::size_t current_{0};
    std::uint64_t size_{0};
};

// The referenced pool and its mapping must outlive this reader. Borrowed views
// become invalid after release_previous_read() or reader destruction; owned
// cross-slice views remain valid independently.
class BufferReader final {
public:
    BufferReader() noexcept = default;
    ~BufferReader();

    BufferReader(const BufferReader&) = delete;
    BufferReader& operator=(const BufferReader&) = delete;
    BufferReader(BufferReader&& other) noexcept;
    BufferReader& operator=(BufferReader&&) = delete;

    [[nodiscard]] explicit operator bool() const noexcept;
    [[nodiscard]] std::uint64_t remaining() const noexcept;
    [[nodiscard]] std::size_t pinned_slice_count() const noexcept;
    [[nodiscard]] BufferIoResult<std::uint8_t> read_byte();
    [[nodiscard]] BufferIoResult<BufferReadView> read_bytes(std::size_t size);
    [[nodiscard]] BufferIoResult<BufferReadView> peek(std::size_t size);
    [[nodiscard]] BufferIoResult<std::string> read_string(std::size_t size);
    [[nodiscard]] BufferIoResult<std::size_t> discard(std::size_t size);
    [[nodiscard]] BufferIoError release_previous_read() noexcept;

private:
    struct SliceCursor {
        BufferAllocation allocation{};
        std::uint32_t read{0};
        std::uint32_t write{0};
    };

    friend BufferIoResult<BufferReader> make_buffer_reader(
        BufferPool&, BufferChain&&);

    explicit BufferReader(BufferPool& pool) noexcept;
    [[nodiscard]] BufferIoError advance_if_exhausted() noexcept;
    [[nodiscard]] BufferIoError recycle_cursor(std::size_t index) noexcept;
    void reset() noexcept;

    BufferPool* pool_{nullptr};
    std::vector<SliceCursor> slices_{};
    std::size_t current_{0};
    std::uint64_t remaining_{0};
    bool current_pinned_{false};
};

using MutableBufferViewResult = BufferIoResult<MutableBufferView>;
using BufferReadViewResult = BufferIoResult<BufferReadView>;
using BufferReaderResult = BufferIoResult<BufferReader>;

[[nodiscard]] const char* to_string(BufferIoError error) noexcept;
[[nodiscard]] BufferReaderResult make_buffer_reader(
    BufferPool& pool, BufferChain&& chain);

}  // namespace shmipc::shm
