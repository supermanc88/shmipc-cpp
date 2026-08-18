#include "shm/buffer_io.hpp"

#include "shm/buffer_layout.hpp"

#include <algorithm>
#include <cstring>
#include <limits>
#include <utility>

namespace shmipc::shm {
namespace {

BufferIoError translate_pool_error(BufferPoolError error) noexcept {
    if (error == BufferPoolError::none) {
        return BufferIoError::none;
    }
    if (error == BufferPoolError::no_buffer) {
        return BufferIoError::no_buffer;
    }
    return BufferIoError::pool_error;
}

}  // namespace

BufferReadView::BufferReadView(const std::uint8_t* data,
                               std::size_t size) noexcept
    : borrowed_(data), size_(size) {}

BufferReadView::BufferReadView(std::vector<std::uint8_t> owned) noexcept
    : size_(owned.size()), owned_(std::move(owned)) {}

const std::uint8_t* BufferReadView::data() const noexcept {
    return owned_.empty() ? borrowed_ : owned_.data();
}

std::size_t BufferReadView::size() const noexcept { return size_; }

bool BufferReadView::is_zero_copy() const noexcept {
    return borrowed_ != nullptr && size_ != 0U;
}

BufferWriter::BufferWriter(BufferPool& pool) noexcept : pool_(&pool) {}

BufferWriter::~BufferWriter() { reset(); }

std::uint64_t BufferWriter::size() const noexcept { return size_; }

std::size_t BufferWriter::slice_count() const noexcept {
    return chain_.allocations.size();
}

BufferIoError BufferWriter::append_allocation(std::uint32_t minimum) {
    auto allocation = pool_->allocate(minimum);
    if (!allocation) {
        return translate_pool_error(allocation.error);
    }
    try {
        chain_.allocations.push_back(std::move(allocation.value));
    } catch (...) {
        static_cast<void>(pool_->recycle(std::move(allocation.value)));
        throw;
    }
    try {
        used_.push_back(0U);
    } catch (...) {
        static_cast<void>(pool_->recycle(
            std::move(chain_.allocations.back())));
        chain_.allocations.pop_back();
        throw;
    }
    current_ = chain_.allocations.size() - 1U;
    return BufferIoError::none;
}

BufferIoError BufferWriter::append_chain(std::uint64_t minimum) {
    auto allocated = pool_->allocate_chain(minimum);
    if (!allocated) {
        return translate_pool_error(allocated.error);
    }
    const auto first = chain_.allocations.size();
    const auto combined = first + allocated.value.allocations.size();
    try {
        chain_.allocations.reserve(combined);
        used_.reserve(combined);
    } catch (...) {
        static_cast<void>(pool_->recycle_chain(std::move(allocated.value)));
        throw;
    }
    for (auto& allocation : allocated.value.allocations) {
        chain_.allocations.push_back(std::move(allocation));
        used_.push_back(0U);
    }
    allocated.value.allocations.clear();
    allocated.value.data_size = 0;
    current_ = first;
    return BufferIoError::none;
}

BufferIoError BufferWriter::write_byte(std::uint8_t value) {
    const auto result = write_bytes(&value, 1U);
    return result.error;
}

BufferIoResult<std::size_t> BufferWriter::write_bytes(
    const std::uint8_t* data, std::size_t size) {
    if (data == nullptr && size != 0U) {
        return {0U, BufferIoError::null_data};
    }
    if (size > std::numeric_limits<std::uint64_t>::max() - size_) {
        return {0U, BufferIoError::invalid_size};
    }
    std::size_t written = 0;
    while (written < size) {
        if (chain_.allocations.empty() ||
            used_[current_] == chain_.allocations[current_].capacity()) {
            const auto remaining = size - written;
            BufferIoError error = BufferIoError::none;
            if (!chain_.allocations.empty() &&
                current_ + 1U < chain_.allocations.size()) {
                ++current_;
            } else if (remaining > pool_->max_slice_capacity()) {
                error = append_chain(remaining);
            } else {
                error = append_allocation(
                    static_cast<std::uint32_t>(remaining));
            }
            if (error != BufferIoError::none) {
                return {written, error};
            }
        }
        auto& allocation = chain_.allocations[current_];
        const auto available = allocation.capacity() - used_[current_];
        const auto count = static_cast<std::uint32_t>(
            std::min<std::size_t>(available, size - written));
        std::memcpy(allocation.data() + used_[current_], data + written, count);
        used_[current_] += count;
        written += count;
        size_ += count;
    }
    chain_.data_size = size_;
    return {written, BufferIoError::none};
}

BufferIoResult<std::size_t> BufferWriter::write_string(
    const std::string& value) {
    return write_bytes(reinterpret_cast<const std::uint8_t*>(value.data()),
                       value.size());
}

MutableBufferViewResult BufferWriter::reserve(std::size_t size) {
    if (size == 0U || size > pool_->max_slice_capacity() ||
        size > std::numeric_limits<std::uint32_t>::max() ||
        size > std::numeric_limits<std::uint64_t>::max() - size_) {
        return {{}, BufferIoError::invalid_size};
    }
    if (chain_.allocations.empty() ||
        chain_.allocations[current_].capacity() - used_[current_] < size) {
        if (!chain_.allocations.empty() &&
            current_ + 1U < chain_.allocations.size() &&
            chain_.allocations[current_ + 1U].capacity() >= size) {
            ++current_;
        } else {
            const auto error =
                append_allocation(static_cast<std::uint32_t>(size));
            if (error != BufferIoError::none) {
                return {{}, error};
            }
        }
    }
    auto& allocation = chain_.allocations[current_];
    auto* const data = allocation.data() + used_[current_];
    used_[current_] += static_cast<std::uint32_t>(size);
    size_ += size;
    chain_.data_size = size_;
    return {{data, size}, BufferIoError::none};
}

PublishedBufferChainResult BufferWriter::publish() noexcept {
    if (pool_ == nullptr) {
        return {{}, BufferPoolError::invalid_allocation};
    }
    auto result = pool_->publish_chain(std::move(chain_), used_);
    if (result) {
        used_.clear();
        current_ = 0;
        size_ = 0;
    }
    return result;
}

void BufferWriter::reset() noexcept {
    if (pool_ != nullptr && chain_) {
        static_cast<void>(pool_->recycle_chain(std::move(chain_)));
    }
    used_.clear();
    current_ = 0;
    size_ = 0;
}

BufferReader::BufferReader(BufferPool& pool) noexcept : pool_(&pool) {}

BufferReader::~BufferReader() { reset(); }

BufferReader::BufferReader(BufferReader&& other) noexcept
    : pool_(other.pool_),
      slices_(std::move(other.slices_)),
      current_(other.current_),
      remaining_(other.remaining_),
      current_pinned_(other.current_pinned_) {
    other.pool_ = nullptr;
    other.current_ = 0;
    other.remaining_ = 0;
    other.current_pinned_ = false;
}

BufferReader::operator bool() const noexcept { return pool_ != nullptr; }

std::uint64_t BufferReader::remaining() const noexcept { return remaining_; }

std::size_t BufferReader::pinned_slice_count() const noexcept {
    std::size_t result = current_pinned_ ? 1U : 0U;
    for (std::size_t index = 0; index < current_; ++index) {
        if (slices_[index].allocation) {
            ++result;
        }
    }
    return result;
}

BufferIoError BufferReader::recycle_cursor(std::size_t index) noexcept {
    if (!slices_[index].allocation) {
        return BufferIoError::none;
    }
    return translate_pool_error(
        pool_->recycle(std::move(slices_[index].allocation)));
}

BufferIoError BufferReader::advance_if_exhausted() noexcept {
    while (current_ < slices_.size() &&
           slices_[current_].read == slices_[current_].write) {
        if (!current_pinned_) {
            const auto error = recycle_cursor(current_);
            if (error != BufferIoError::none) {
                return error;
            }
        }
        ++current_;
        current_pinned_ = false;
    }
    return BufferIoError::none;
}

BufferIoResult<std::uint8_t> BufferReader::read_byte() {
    if (remaining_ == 0U) {
        return {0U, BufferIoError::out_of_range};
    }
    const auto error = advance_if_exhausted();
    if (error != BufferIoError::none || current_ >= slices_.size()) {
        return {0U, error == BufferIoError::none
                        ? BufferIoError::invalid_state
                        : error};
    }
    auto& current = slices_[current_];
    const auto value = current.allocation.data()[current.read];
    ++current.read;
    --remaining_;
    return {value, BufferIoError::none};
}

BufferReadViewResult BufferReader::read_bytes(std::size_t size) {
    if (size == 0U) {
        return {BufferReadView{}, BufferIoError::none};
    }
    if (size > remaining_) {
        return {{}, BufferIoError::out_of_range};
    }
    const auto advance_error = advance_if_exhausted();
    if (advance_error != BufferIoError::none || current_ >= slices_.size()) {
        return {{}, advance_error == BufferIoError::none
                         ? BufferIoError::invalid_state
                         : advance_error};
    }
    auto& current = slices_[current_];
    const auto available = current.write - current.read;
    if (size <= available) {
        const auto* const data = current.allocation.data() + current.read;
        current.read += static_cast<std::uint32_t>(size);
        remaining_ -= size;
        current_pinned_ = true;
        return {BufferReadView(data, size), BufferIoError::none};
    }

    std::vector<std::uint8_t> result;
    result.reserve(size);
    auto left = size;
    while (left > 0U) {
        const auto error = advance_if_exhausted();
        if (error != BufferIoError::none || current_ >= slices_.size()) {
            return {{}, error == BufferIoError::none
                             ? BufferIoError::invalid_state
                             : error};
        }
        auto& slice = slices_[current_];
        const auto count = static_cast<std::uint32_t>(
            std::min<std::size_t>(slice.write - slice.read, left));
        result.insert(result.end(), slice.allocation.data() + slice.read,
                      slice.allocation.data() + slice.read + count);
        slice.read += count;
        left -= count;
    }
    remaining_ -= size;
    return {BufferReadView(std::move(result)), BufferIoError::none};
}

BufferReadViewResult BufferReader::peek(std::size_t size) {
    if (size == 0U) {
        return {BufferReadView{}, BufferIoError::none};
    }
    if (size > remaining_) {
        return {{}, BufferIoError::out_of_range};
    }
    const auto advance_error = advance_if_exhausted();
    if (advance_error != BufferIoError::none || current_ >= slices_.size()) {
        return {{}, advance_error == BufferIoError::none
                         ? BufferIoError::invalid_state
                         : advance_error};
    }
    const auto& current = slices_[current_];
    const auto available = current.write - current.read;
    if (size <= available) {
        current_pinned_ = true;
        return {BufferReadView(current.allocation.data() + current.read, size),
                BufferIoError::none};
    }

    std::vector<std::uint8_t> result;
    result.reserve(size);
    auto left = size;
    auto index = current_;
    auto read = slices_[index].read;
    while (left > 0U && index < slices_.size()) {
        const auto& slice = slices_[index];
        const auto count = static_cast<std::uint32_t>(
            std::min<std::size_t>(slice.write - read, left));
        result.insert(result.end(), slice.allocation.data() + read,
                      slice.allocation.data() + read + count);
        left -= count;
        ++index;
        if (index < slices_.size()) {
            read = slices_[index].read;
        }
    }
    if (left != 0U) {
        return {{}, BufferIoError::invalid_state};
    }
    return {BufferReadView(std::move(result)), BufferIoError::none};
}

BufferIoResult<std::string> BufferReader::read_string(std::size_t size) {
    if (size == 0U) {
        return {std::string{}, BufferIoError::none};
    }
    if (size > remaining_) {
        return {{}, BufferIoError::out_of_range};
    }
    std::string result(size, '\0');
    std::size_t written = 0;
    while (written < size) {
        const auto error = advance_if_exhausted();
        if (error != BufferIoError::none || current_ >= slices_.size()) {
            return {{}, error == BufferIoError::none
                            ? BufferIoError::invalid_state
                            : error};
        }
        auto& slice = slices_[current_];
        const auto count = static_cast<std::uint32_t>(
            std::min<std::size_t>(slice.write - slice.read, size - written));
        std::memcpy(result.data() + written,
                    slice.allocation.data() + slice.read, count);
        slice.read += count;
        written += count;
    }
    remaining_ -= size;
    return {std::move(result), BufferIoError::none};
}

BufferIoResult<std::size_t> BufferReader::discard(std::size_t size) {
    if (size > remaining_) {
        return {0U, BufferIoError::out_of_range};
    }
    auto left = size;
    while (left > 0U) {
        const auto error = advance_if_exhausted();
        if (error != BufferIoError::none || current_ >= slices_.size()) {
            return {size - left, error == BufferIoError::none
                                     ? BufferIoError::invalid_state
                                     : error};
        }
        auto& slice = slices_[current_];
        const auto count = static_cast<std::uint32_t>(
            std::min<std::size_t>(slice.write - slice.read, left));
        slice.read += count;
        left -= count;
    }
    remaining_ -= size;
    return {size, BufferIoError::none};
}

BufferIoError BufferReader::release_previous_read() noexcept {
    for (std::size_t index = 0; index < current_; ++index) {
        const auto error = recycle_cursor(index);
        if (error != BufferIoError::none) {
            return error;
        }
    }
    current_pinned_ = false;
    return advance_if_exhausted();
}

void BufferReader::reset() noexcept {
    if (pool_ != nullptr) {
        for (std::size_t index = 0; index < slices_.size(); ++index) {
            static_cast<void>(recycle_cursor(index));
        }
    }
    slices_.clear();
    current_ = 0;
    remaining_ = 0;
    current_pinned_ = false;
}

const char* to_string(BufferIoError error) noexcept {
    switch (error) {
        case BufferIoError::none:
            return "none";
        case BufferIoError::null_data:
            return "null data";
        case BufferIoError::invalid_size:
            return "invalid size";
        case BufferIoError::no_buffer:
            return "no buffer";
        case BufferIoError::invalid_state:
            return "invalid state";
        case BufferIoError::out_of_range:
            return "out of range";
        case BufferIoError::pool_error:
            return "buffer pool error";
    }
    return "unknown buffer IO error";
}

BufferReaderResult make_buffer_reader(BufferPool& pool, BufferChain&& chain) {
    if (!chain || chain.data_size == 0U) {
        return {{}, BufferIoError::invalid_state};
    }
    std::uint64_t total = 0;
    for (const auto& allocation : chain.allocations) {
        const auto* const header_memory =
            allocation.data() - buffer_slice_header_size;
        const auto header = read_buffer_slice_header(
            header_memory, buffer_slice_header_size + allocation.capacity());
        if (!header || header.value.capacity != allocation.capacity()) {
            static_cast<void>(pool.recycle_chain(std::move(chain)));
            return {{}, BufferIoError::invalid_state};
        }
        total += header.value.size;
    }
    if (total != chain.data_size) {
        static_cast<void>(pool.recycle_chain(std::move(chain)));
        return {{}, BufferIoError::invalid_state};
    }

    BufferReader reader(pool);
    try {
        reader.slices_.reserve(chain.allocations.size());
    } catch (...) {
        static_cast<void>(pool.recycle_chain(std::move(chain)));
        throw;
    }
    for (auto& allocation : chain.allocations) {
        const auto header = read_buffer_slice_header(
            allocation.data() - buffer_slice_header_size,
            buffer_slice_header_size + allocation.capacity());
        reader.slices_.push_back(
            {std::move(allocation), header.value.data_start,
             header.value.data_start + header.value.size});
    }
    reader.remaining_ = total;
    chain.allocations.clear();
    chain.data_size = 0;
    return {std::move(reader), BufferIoError::none};
}

}  // namespace shmipc::shm
