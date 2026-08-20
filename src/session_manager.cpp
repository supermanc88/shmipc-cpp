#include "shmipc/session_manager.hpp"

#include "public/session_impl.hpp"

#include <atomic>
#include <cerrno>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include <unistd.h>

namespace shmipc {
namespace {

enum class EndpointKind {
    tcp,
    unix_socket,
};

struct ManagerPool final {
    ManagerPool(std::size_t value_index, std::size_t maximum_idle) noexcept
        : index(value_index), max_idle_streams(maximum_idle) {}

    std::mutex mutex{};
    Session session{};
    std::deque<Stream> idle_streams{};
    const std::size_t index;
    const std::size_t max_idle_streams;
    std::uint64_t generation{0U};
    bool active{true};
};

bool valid_config(const SessionManagerConfig& config) noexcept {
    return config.session_count != 0U && config.round_robin_batch != 0U &&
           config.reconnect_interval.count() > 0 &&
           config.health_check_interval.count() > 0;
}

ClientConfig session_config(const SessionManagerConfig& config,
                            std::size_t session_index,
                            std::uint64_t generation) {
    auto result = config.client_config;
    const auto suffix = ".pid-" + std::to_string(::getpid()) + ".session-" +
                        std::to_string(session_index) + ".generation-" +
                        std::to_string(generation);
    result.queue_name += suffix;
    result.buffer_name += suffix;
    return result;
}

SessionResult connect_session(EndpointKind kind, const std::string& address,
                              std::uint16_t port,
                              const SessionManagerConfig& config,
                              std::size_t session_index,
                              std::uint64_t generation) {
    auto client = session_config(config, session_index, generation);
    return kind == EndpointKind::tcp
               ? connect_tcp(address, port, client)
               : connect_unix(address, client);
}

void close_streams(std::deque<Stream>& streams) noexcept {
    while (!streams.empty()) {
        auto stream = std::move(streams.front());
        streams.pop_front();
        static_cast<void>(stream.close());
    }
}

}  // namespace

struct PooledStream::Impl final {
    Impl(Stream&& value, std::weak_ptr<ManagerPool> owner,
         std::size_t owner_index, std::uint64_t owner_generation) noexcept
        : stream(std::move(value)),
          pool(std::move(owner)),
          index(owner_index),
          generation(owner_generation) {}

    Stream stream{};
    std::weak_ptr<ManagerPool> pool{};
    std::size_t index{0U};
    std::uint64_t generation{0U};
};

struct SessionManager::Impl final {
    Impl(EndpointKind endpoint_kind, std::string endpoint_address,
         std::uint16_t endpoint_port, SessionManagerConfig manager_config)
        : kind(endpoint_kind),
          address(std::move(endpoint_address)),
          port(endpoint_port),
          config(std::move(manager_config)) {}

    ~Impl() {
        shutdown();
    }

    Status initialize() {
        pools.reserve(config.session_count);
        for (std::size_t index = 0U; index < config.session_count; ++index) {
            auto connected =
                connect_session(kind, address, port, config, index, 0U);
            if (!connected) {
                return connected.status;
            }
            auto pool = std::make_shared<ManagerPool>(
                index, config.max_idle_streams_per_session);
            pool->session = std::move(connected.value);
            pools.push_back(std::move(pool));
        }
        start_workers();
        return {};
    }

    void start_workers() {
        workers.reserve(pools.size());
        for (const auto& pool : pools) {
            workers.emplace_back([this, pool] { monitor(pool); });
        }
    }

    bool wait_for(std::chrono::milliseconds duration) {
        std::unique_lock<std::mutex> lock(stop_mutex);
        return stop_condition.wait_for(lock, duration, [&] {
            return stopping.load(std::memory_order_acquire);
        });
    }

    void monitor(const std::shared_ptr<ManagerPool>& pool) {
        while (!wait_for(config.health_check_interval)) {
            Session old_session;
            std::deque<Stream> old_idle;
            std::uint64_t next_generation = 0U;
            {
                std::lock_guard<std::mutex> lock(pool->mutex);
                if (!pool->active || pool->session.is_open()) {
                    continue;
                }
                ++pool->generation;
                next_generation = pool->generation;
                old_session = std::move(pool->session);
                old_idle.swap(pool->idle_streams);
            }
            close_streams(old_idle);
            static_cast<void>(old_session.close());

            while (!stopping.load(std::memory_order_acquire)) {
                if (wait_for(config.reconnect_interval)) {
                    return;
                }
                auto connected = connect_session(
                    kind, address, port, config, pool->index, next_generation);
                if (!connected) {
                    continue;
                }
                std::lock_guard<std::mutex> lock(pool->mutex);
                if (!pool->active ||
                    stopping.load(std::memory_order_acquire) ||
                    pool->generation != next_generation) {
                    static_cast<void>(connected.value.close());
                    return;
                }
                pool->session = std::move(connected.value);
                break;
            }
        }
    }

    void shutdown() noexcept {
        if (stopping.exchange(true, std::memory_order_acq_rel)) {
            return;
        }
        stop_condition.notify_all();
        for (auto& worker : workers) {
            if (worker.joinable()) {
                worker.join();
            }
        }
        workers.clear();
        for (const auto& pool : pools) {
            Session session;
            std::deque<Stream> idle;
            {
                std::lock_guard<std::mutex> lock(pool->mutex);
                pool->active = false;
                ++pool->generation;
                session = std::move(pool->session);
                idle.swap(pool->idle_streams);
            }
            close_streams(idle);
            static_cast<void>(session.close());
        }
    }

    EndpointKind kind;
    std::string address{};
    std::uint16_t port{0U};
    SessionManagerConfig config{};
    std::vector<std::shared_ptr<ManagerPool>> pools{};
    std::vector<std::thread> workers{};
    std::atomic<std::uint64_t> selection_count{0U};
    std::atomic<bool> stopping{false};
    std::mutex stop_mutex{};
    std::condition_variable stop_condition{};
};

PooledStream::PooledStream() noexcept = default;
PooledStream::PooledStream(std::unique_ptr<Impl> impl) noexcept
    : impl_(std::move(impl)) {}
PooledStream::~PooledStream() {
    static_cast<void>(return_to_pool());
}
PooledStream::PooledStream(PooledStream&&) noexcept = default;
PooledStream& PooledStream::operator=(PooledStream&& other) noexcept {
    if (this != &other) {
        static_cast<void>(return_to_pool());
        impl_ = std::move(other.impl_);
    }
    return *this;
}

PooledStream::operator bool() const noexcept {
    return impl_ != nullptr && impl_->stream.is_open();
}

Stream& PooledStream::stream() noexcept {
    return impl_->stream;
}

const Stream& PooledStream::stream() const noexcept {
    return impl_->stream;
}

std::size_t PooledStream::session_index() const noexcept {
    return impl_ == nullptr ? 0U : impl_->index;
}

Status PooledStream::return_to_pool() noexcept {
    auto lease = std::move(impl_);
    if (lease == nullptr || !lease->stream) {
        return {};
    }
    auto pool = lease->pool.lock();
    bool reusable = pool != nullptr && lease->stream.impl_ != nullptr;
    if (reusable) {
        std::lock_guard<std::mutex> callback_lock(
            lease->stream.impl_->callback_mutex);
        reusable = lease->stream.impl_->callback_control.expired();
    }
    if (reusable) {
        reusable = lease->stream.impl_->stream.reset_for_reuse();
    }
    if (reusable) {
        std::lock_guard<std::mutex> lock(pool->mutex);
        reusable = pool->active && pool->generation == lease->generation &&
                   pool->session.is_open() && pool->session.is_healthy() &&
                   pool->idle_streams.size() < pool->max_idle_streams;
        if (reusable) {
            pool->idle_streams.push_back(std::move(lease->stream));
            return {};
        }
    }
    return lease->stream.close();
}

Status PooledStream::close() noexcept {
    auto lease = std::move(impl_);
    return lease == nullptr ? Status{} : lease->stream.close();
}

SessionManager::SessionManager() noexcept = default;
SessionManager::SessionManager(std::shared_ptr<Impl> impl) noexcept
    : impl_(std::move(impl)) {}
SessionManager::~SessionManager() {
    static_cast<void>(close());
}
SessionManager::SessionManager(SessionManager&&) noexcept = default;
SessionManager& SessionManager::operator=(SessionManager&& other) noexcept {
    if (this != &other) {
        static_cast<void>(close());
        impl_ = std::move(other.impl_);
    }
    return *this;
}

SessionManager::operator bool() const noexcept {
    return impl_ != nullptr &&
           !impl_->stopping.load(std::memory_order_acquire);
}

std::size_t SessionManager::session_count() const noexcept {
    return impl_ == nullptr ? 0U : impl_->pools.size();
}

PooledStreamResult SessionManager::get_stream() {
    if (impl_ == nullptr ||
        impl_->stopping.load(std::memory_order_acquire) ||
        impl_->pools.empty()) {
        return {{}, detail::make_status(Error::closed, EBADF)};
    }
    const auto selection =
        impl_->selection_count.fetch_add(1U, std::memory_order_relaxed) + 1U;
    const auto pool_index =
        (selection / impl_->config.round_robin_batch) % impl_->pools.size();
    const auto& pool = impl_->pools[pool_index];
    std::lock_guard<std::mutex> lock(pool->mutex);
    if (!pool->active || !pool->session.is_open()) {
        return {{}, detail::make_status(Error::closed, ENOTCONN)};
    }
    if (!pool->session.is_healthy()) {
        return {{}, detail::make_status(Error::unhealthy)};
    }
    Stream stream;
    while (!pool->idle_streams.empty()) {
        stream = std::move(pool->idle_streams.front());
        pool->idle_streams.pop_front();
        bool reusable = stream.impl_ != nullptr;
        if (reusable) {
            std::lock_guard<std::mutex> callback_lock(
                stream.impl_->callback_mutex);
            reusable = stream.impl_->callback_control.expired();
        }
        if (reusable && stream.impl_->stream.reset_for_reuse()) {
            break;
        }
        static_cast<void>(stream.close());
    }
    if (!stream.is_open()) {
        auto opened = pool->session.open_stream();
        if (!opened) {
            return {{}, opened.status};
        }
        stream = std::move(opened.value);
    }
    return {PooledStream(std::make_unique<PooledStream::Impl>(
                std::move(stream), pool, pool_index, pool->generation)), {}};
}

Status SessionManager::close() noexcept {
    auto impl = impl_;
    if (impl != nullptr) {
        impl->shutdown();
    }
    return {};
}

SessionManagerResult make_tcp_session_manager(
    const std::string& host, std::uint16_t port,
    const SessionManagerConfig& config) {
    if (host.empty() || !valid_config(config)) {
        return {{}, detail::make_status(Error::invalid_argument, EINVAL)};
    }
    if (config.client_config.shared_memory_mode == SharedMemoryMode::memfd) {
        return {{}, detail::make_status(Error::unsupported, ENOTSUP)};
    }
    auto impl = std::make_shared<SessionManager::Impl>(
        EndpointKind::tcp, host, port, config);
    const auto status = impl->initialize();
    return status ? SessionManagerResult{SessionManager(std::move(impl)), {}}
                  : SessionManagerResult{{}, status};
}

SessionManagerResult make_unix_session_manager(
    const std::string& path, const SessionManagerConfig& config) {
    if (path.empty() || !valid_config(config)) {
        return {{}, detail::make_status(Error::invalid_argument, EINVAL)};
    }
    auto impl = std::make_shared<SessionManager::Impl>(
        EndpointKind::unix_socket, path, 0U, config);
    const auto status = impl->initialize();
    return status ? SessionManagerResult{SessionManager(std::move(impl)), {}}
                  : SessionManagerResult{{}, status};
}

}  // namespace shmipc
