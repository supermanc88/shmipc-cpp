#include "shm/buffer_io.hpp"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

namespace {

using shmipc::shm::BufferIoError;

std::vector<std::uint8_t> expected_payload() {
    std::vector<std::uint8_t> result;
    for (std::uint8_t value = 0; value < 16U; ++value) {
        result.push_back(value);
    }
    for (std::uint32_t index = 0; index < 300U; ++index) {
        result.push_back(static_cast<std::uint8_t>((index * 17U) & 0xffU));
    }
    result.insert(result.end(), {'t', 'a', 'i', 'l'});
    return result;
}

bool equals(const shmipc::shm::BufferReadView& view,
            const std::vector<std::uint8_t>& expected,
            std::size_t offset) {
    return view.size() <= expected.size() - offset &&
           std::equal(view.data(), view.data() + view.size(),
                      expected.data() + offset);
}

bool test_write_read_and_pin() {
    std::vector<std::uint8_t> memory(1U << 20U, 0);
    auto creator = shmipc::shm::initialize_buffer_pool(
        memory.data(), memory.size(), {{64U, 50U}, {128U, 50U}},
        shmipc::shm::BufferListRole::creator);
    auto mapper = creator ? shmipc::shm::map_buffer_pool(
                                memory.data(), memory.size(),
                                shmipc::shm::BufferListRole::mapper)
                          : shmipc::shm::BufferPoolCreateResult{};
    if (!creator || !mapper) {
        return false;
    }
    const auto baseline = creator.value.available_bytes();
    const auto expected = expected_payload();

    shmipc::shm::BufferWriter writer(creator.value);
    auto reserved = writer.reserve(16U);
    if (!reserved) {
        return false;
    }
    for (std::uint8_t value = 0; value < 16U; ++value) {
        reserved.value.data[value] = value;
    }
    const auto middle = expected.data() + 16U;
    const auto written = writer.write_bytes(middle, 300U);
    const auto tail = writer.write_string("tail");
    if (!written || written.value != 300U || !tail || tail.value != 4U ||
        writer.size() != expected.size() || writer.slice_count() != 3U) {
        return false;
    }
    const auto published = writer.publish();
    if (!published || published.value.data_size != expected.size() ||
        published.value.slice_count != 3U || writer.size() != 0U) {
        return false;
    }
    auto chain = mapper.value.adopt_chain(published.value.root_offset);
    auto reader = chain ? shmipc::shm::make_buffer_reader(
                              mapper.value, std::move(chain.value))
                        : shmipc::shm::BufferReaderResult{};
    if (!chain || !reader || reader.value.remaining() != expected.size()) {
        return false;
    }

    const auto peeked = reader.value.peek(8U);
    const auto crossed_peek = reader.value.peek(100U);
    const auto first = reader.value.read_bytes(64U);
    const auto second = reader.value.read_bytes(120U);
    const auto crossed = reader.value.read_bytes(20U);
    if (!peeked || !peeked.value.is_zero_copy() ||
        !equals(peeked.value, expected, 0U) || !first ||
        !crossed_peek || crossed_peek.value.is_zero_copy() ||
        !equals(crossed_peek.value, expected, 0U) ||
        !first.value.is_zero_copy() || !equals(first.value, expected, 0U) ||
        !second || !second.value.is_zero_copy() ||
        !equals(second.value, expected, 64U) || !crossed ||
        crossed.value.is_zero_copy() || !equals(crossed.value, expected, 184U) ||
        reader.value.pinned_slice_count() != 2U ||
        reader.value.remaining() != 116U) {
        return false;
    }
    const auto before_release = mapper.value.available_bytes();
    if (reader.value.release_previous_read() != BufferIoError::none ||
        reader.value.pinned_slice_count() != 0U ||
        mapper.value.available_bytes() != before_release + 192U) {
        return false;
    }
    const auto byte = reader.value.read_byte();
    const auto text = reader.value.read_string(3U);
    const auto pinned_after_copy = reader.value.pinned_slice_count();
    const auto discarded = reader.value.discard(10U);
    const auto rest = reader.value.read_bytes(102U);
    if (!byte || byte.value != expected[204U] || !text ||
        text.value != std::string(expected.begin() + 205U,
                                 expected.begin() + 208U) ||
        pinned_after_copy != 0U ||
        !discarded || discarded.value != 10U || !rest ||
        !rest.value.is_zero_copy() || !equals(rest.value, expected, 218U) ||
        reader.value.remaining() != 0U ||
        reader.value.read_byte().error != BufferIoError::out_of_range ||
        reader.value.peek(1U).error != BufferIoError::out_of_range) {
        return false;
    }
    return reader.value.release_previous_read() == BufferIoError::none &&
           mapper.value.available_bytes() == baseline;
}

bool test_raii_rollback_and_errors() {
    std::vector<std::uint8_t> memory(64U << 10U, 0);
    auto pool = shmipc::shm::initialize_buffer_pool(
        memory.data(), memory.size(), {{64U, 100U}});
    if (!pool) {
        return false;
    }
    {
        shmipc::shm::BufferWriter writer(pool.value);
        const std::uint8_t bytes[] = {1U, 2U, 3U};
        if (!writer.write_bytes(bytes, sizeof(bytes)) ||
            writer.reserve(65U).error != BufferIoError::invalid_size ||
            writer.write_bytes(nullptr, 1U).error != BufferIoError::null_data) {
            return false;
        }
    }
    return pool.value.all_returned();
}

bool test_reader_raii_recycles_pins() {
    std::vector<std::uint8_t> memory(64U << 10U, 0);
    auto pool = shmipc::shm::initialize_buffer_pool(
        memory.data(), memory.size(), {{64U, 100U}});
    if (!pool) {
        return false;
    }
    std::uint32_t root = 0;
    {
        shmipc::shm::BufferWriter writer(pool.value);
        std::vector<std::uint8_t> bytes(100U, 0x5aU);
        const auto written = writer.write_bytes(bytes.data(), bytes.size());
        const auto published = written ? writer.publish()
                                       : shmipc::shm::PublishedBufferChainResult{};
        if (!published) {
            return false;
        }
        root = published.value.root_offset;
    }
    {
        auto chain = pool.value.adopt_chain(root);
        auto reader = chain ? shmipc::shm::make_buffer_reader(
                                  pool.value, std::move(chain.value))
                            : shmipc::shm::BufferReaderResult{};
        const auto view = reader ? reader.value.read_bytes(32U)
                                 : shmipc::shm::BufferReadViewResult{};
        if (!reader || !view || !view.value.is_zero_copy() ||
            reader.value.pinned_slice_count() != 1U) {
            return false;
        }
    }
    return pool.value.all_returned();
}

bool test_reader_advances_one_slice_at_a_time() {
    std::vector<std::uint8_t> memory(64U << 10U, 0);
    auto pool = shmipc::shm::initialize_buffer_pool(
        memory.data(), memory.size(), {{8U, 100U}});
    if (!pool) {
        return false;
    }
    std::vector<std::uint8_t> expected(20U);
    for (std::size_t index = 0; index < expected.size(); ++index) {
        expected[index] = static_cast<std::uint8_t>(index);
    }
    shmipc::shm::BufferWriter writer(pool.value);
    const auto written = writer.write_bytes(expected.data(), expected.size());
    const auto published = written ? writer.publish()
                                   : shmipc::shm::PublishedBufferChainResult{};
    auto chain = published
                     ? pool.value.adopt_chain(published.value.root_offset)
                     : shmipc::shm::BufferChainResult{};
    auto reader = chain ? shmipc::shm::make_buffer_reader(
                              pool.value, std::move(chain.value))
                        : shmipc::shm::BufferReaderResult{};
    if (!reader) {
        return false;
    }
    for (const auto expected_byte : expected) {
        const auto byte = reader.value.read_byte();
        if (!byte || byte.value != expected_byte) {
            return false;
        }
    }
    return reader.value.remaining() == 0U &&
           reader.value.release_previous_read() == BufferIoError::none &&
           pool.value.all_returned();
}

}  // namespace

int main() {
    if (!test_write_read_and_pin()) {
        std::cerr << "buffer IO write/read/pin test failed\n";
        return 1;
    }
    if (!test_raii_rollback_and_errors()) {
        std::cerr << "buffer IO RAII/error test failed\n";
        return 1;
    }
    if (!test_reader_raii_recycles_pins()) {
        std::cerr << "buffer IO reader RAII test failed\n";
        return 1;
    }
    if (!test_reader_advances_one_slice_at_a_time()) {
        std::cerr << "buffer IO slice advance test failed\n";
        return 1;
    }
    return 0;
}
