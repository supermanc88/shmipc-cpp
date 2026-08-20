#include <shmipc/listener.hpp>
#include <shmipc/session.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <future>
#include <iostream>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include <unistd.h>

namespace {

using namespace std::chrono_literals;

struct TestDirectory {
    std::array<char, 64> storage{};
    std::string path{};

    bool create() {
        const std::string pattern = "/tmp/shmipc-observability.XXXXXX";
        std::copy(pattern.begin(), pattern.end(), storage.begin());
        auto* const directory = ::mkdtemp(storage.data());
        if (directory == nullptr) {
            return false;
        }
        path = directory;
        return true;
    }

    ~TestDirectory() {
        if (!path.empty()) {
            static_cast<void>(::rmdir(path.c_str()));
        }
    }
};

class CapturingMonitor final : public shmipc::Monitor {
public:
    explicit CapturingMonitor(shmipc::Status result = {})
        : flush_result_(result) {}

    void on_session_metrics(const shmipc::SessionMetrics& metrics) override {
        std::lock_guard<std::mutex> lock(mutex_);
        snapshots_.push_back(metrics);
        condition_.notify_all();
    }

    shmipc::Status flush() override {
        std::lock_guard<std::mutex> lock(mutex_);
        ++flush_count_;
        condition_.notify_all();
        return flush_result_;
    }

    bool wait_for_snapshots(std::size_t count,
                            std::chrono::milliseconds timeout) {
        std::unique_lock<std::mutex> lock(mutex_);
        return condition_.wait_for(lock, timeout,
                                   [&] { return snapshots_.size() >= count; });
    }

    shmipc::SessionMetrics last_snapshot() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return snapshots_.empty() ? shmipc::SessionMetrics{}
                                  : snapshots_.back();
    }

    std::size_t snapshot_count() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return snapshots_.size();
    }

    std::size_t flush_count() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return flush_count_;
    }

private:
    const shmipc::Status flush_result_{};
    mutable std::mutex mutex_{};
    std::condition_variable condition_{};
    std::vector<shmipc::SessionMetrics> snapshots_{};
    std::size_t flush_count_{0U};
};

class CapturingLogger final : public shmipc::Logger {
public:
    void log(shmipc::LogLevel level, const std::string& component,
             const std::string& message) override {
        std::lock_guard<std::mutex> lock(mutex_);
        records_.push_back({level, component, message});
    }

    bool contains(shmipc::LogLevel level, const std::string& text) const {
        std::lock_guard<std::mutex> lock(mutex_);
        return std::any_of(records_.begin(), records_.end(),
                           [&](const Record& record) {
                               return record.level == level &&
                                      record.message.find(text) !=
                                          std::string::npos;
                           });
    }

    bool contains_component(const std::string& component) const {
        std::lock_guard<std::mutex> lock(mutex_);
        return std::any_of(records_.begin(), records_.end(),
                           [&](const Record& record) {
                               return record.component == component;
                           });
    }

private:
    struct Record {
        shmipc::LogLevel level;
        std::string component;
        std::string message;
    };

    mutable std::mutex mutex_{};
    std::vector<Record> records_{};
};

bool test_metrics_flush_and_logging() {
    TestDirectory directory;
    if (!directory.create()) {
        return false;
    }
    auto client_monitor = std::make_shared<CapturingMonitor>(
        shmipc::Status{shmipc::Error::callback_error, 0});
    auto server_monitor = std::make_shared<CapturingMonitor>();
    auto logger = std::make_shared<CapturingLogger>();

    shmipc::ListenerConfig listener_config;
    listener_config.monitor = server_monitor;
    listener_config.metrics_interval = 20ms;
    listener_config.logger = logger;
    listener_config.log_level = shmipc::LogLevel::info;
    auto listener = shmipc::listen_tcp("127.0.0.1", 0U, listener_config);
    if (!listener) {
        return listener.status.error == shmipc::Error::unsupported;
    }
    auto accepted_future = std::async(std::launch::async, [&] {
        return listener.value.accept_session(5s);
    });

    shmipc::ClientConfig client_config;
    client_config.queue_name = directory.path + "/queue";
    client_config.buffer_name = directory.path + "/buffer";
    client_config.queue_capacity = 64U;
    client_config.buffer_size = 1U << 20U;
    client_config.buffer_tiers = {{4096U, 100U}};
    client_config.monitor = client_monitor;
    client_config.metrics_interval = 20ms;
    client_config.logger = logger;
    client_config.log_level = shmipc::LogLevel::info;
    auto client = shmipc::connect_tcp(
        "127.0.0.1", listener.value.local_port(), client_config);
    auto server = accepted_future.get();
    if (!client || !server) {
        return false;
    }
    auto client_stream = client.value.open_stream();
    const std::vector<std::uint8_t> shared_payload{1U, 2U, 3U, 4U};
    if (!client_stream || !client_stream.value.send(shared_payload)) {
        return false;
    }
    auto server_stream = server.value.accept_stream(5s);
    auto shared_received =
        server_stream ? server_stream.value.receive(5s) : shmipc::MessageResult{};
    if (!shared_received || shared_received.value != shared_payload) {
        return false;
    }

    const std::vector<std::uint8_t> fallback_payload(2U << 20U, 0x5aU);
    if (!client_stream.value.send(fallback_payload)) {
        return false;
    }
    auto fallback_received = server_stream.value.receive(5s);
    if (!fallback_received || fallback_received.value != fallback_payload) {
        return false;
    }

    const auto client_metrics = client.value.metrics();
    const auto server_metrics = server.value.metrics();
    const auto total_bytes = shared_payload.size() + fallback_payload.size();
    if (client_metrics.session_id == 0U || server_metrics.session_id == 0U ||
        client_metrics.session_id == server_metrics.session_id ||
        !client_metrics.is_client || server_metrics.is_client ||
        client_metrics.protocol_version != 2U ||
        server_metrics.protocol_version != 2U ||
        client_metrics.performance.bytes_sent != total_bytes ||
        server_metrics.performance.bytes_received != total_bytes ||
        client_metrics.performance.sent_polling_events == 0U ||
        server_metrics.performance.received_polling_events == 0U ||
        client_metrics.stability.shared_memory_allocation_errors == 0U ||
        client_metrics.stability.fallback_writes == 0U ||
        server_metrics.stability.fallback_reads == 0U ||
        client_metrics.stability.active_streams != 1U ||
        server_metrics.stability.active_streams != 1U ||
        client_metrics.shared_memory.capacity_bytes == 0U ||
        client_metrics.shared_memory.capacity_bytes !=
            server_metrics.shared_memory.capacity_bytes ||
        client_metrics.shared_memory.used_bytes != 0U ||
        server_metrics.shared_memory.used_bytes != 0U ||
        !logger->contains_component(
            "session." + std::to_string(client_metrics.session_id) +
            ".stream." + std::to_string(client_stream.value.id())) ||
        !logger->contains_component(
            "session." + std::to_string(server_metrics.session_id) +
            ".stream." + std::to_string(server_stream.value.id())) ||
        !logger->contains(shmipc::LogLevel::warning,
                          "entered fallback mode while sending") ||
        !logger->contains(shmipc::LogLevel::warning,
                          "entered fallback mode while receiving")) {
        return false;
    }
    if (!client_monitor->wait_for_snapshots(1U, 2s) ||
        !server_monitor->wait_for_snapshots(1U, 2s)) {
        return false;
    }
    const auto client_periodic_count = client_monitor->snapshot_count();
    const auto server_periodic_count = server_monitor->snapshot_count();

    static_cast<void>(client_stream.value.close());
    static_cast<void>(server_stream.value.wait_remote_close(5s));
    static_cast<void>(server_stream.value.close());
    const auto client_close = client.value.close();
    const auto server_close = server.value.close();
    static_cast<void>(listener.value.close());
    const auto client_final = client_monitor->last_snapshot();
    const auto server_final = server_monitor->last_snapshot();
    return client_close && server_close &&
           client_monitor->flush_count() == 1U &&
           server_monitor->flush_count() == 1U &&
           client_monitor->snapshot_count() > client_periodic_count &&
           server_monitor->snapshot_count() > server_periodic_count &&
           client_final.performance.bytes_sent == total_bytes &&
           server_final.performance.bytes_received == total_bytes &&
           client_final.stability.active_streams == 0U &&
           server_final.stability.active_streams == 0U &&
           logger->contains(shmipc::LogLevel::error,
                            "monitor flush failed") &&
           logger->contains(shmipc::LogLevel::info, "closed session");
}

bool test_invalid_observability_config() {
    shmipc::ClientConfig client_config;
    client_config.metrics_interval = 0ms;
    if (shmipc::connect_tcp("127.0.0.1", 1U, client_config).status.error !=
        shmipc::Error::invalid_argument) {
        return false;
    }
    shmipc::ListenerConfig listener_config;
    listener_config.metrics_interval = 0ms;
    return shmipc::listen_tcp("127.0.0.1", 0U, listener_config).status.error ==
               shmipc::Error::invalid_argument &&
           std::string(shmipc::to_string(shmipc::LogLevel::warning)) ==
               "warning";
}

}  // namespace

int main() {
    if (!test_invalid_observability_config() ||
        !test_metrics_flush_and_logging()) {
        std::cerr << "public observability test failed\n";
        return 1;
    }
    return 0;
}
