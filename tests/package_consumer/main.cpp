#include <shmipc/listener.hpp>
#include <shmipc/session.hpp>
#include <shmipc/session_manager.hpp>
#include <shmipc/stream_connection.hpp>
#include <shmipc/version.hpp>

#include <chrono>
#include <memory>
#include <type_traits>
#include <vector>

static_assert(!std::is_copy_constructible<shmipc::Session>::value, "RAII owner");
static_assert(std::is_nothrow_move_constructible<shmipc::Session>::value,
              "session must transfer ownership without throwing");
static_assert(!std::is_copy_constructible<shmipc::Stream>::value, "RAII owner");
static_assert(std::is_nothrow_move_constructible<shmipc::Stream>::value,
              "stream must transfer ownership without throwing");
static_assert(!std::is_copy_constructible<shmipc::CallbackExecutor>::value,
              "executor owns worker threads");
static_assert(
    std::is_nothrow_move_constructible<shmipc::CallbackSubscription>::value,
    "subscription must transfer ownership without throwing");
static_assert(!std::is_copy_constructible<shmipc::Listener>::value,
              "listener owns a control endpoint");
static_assert(std::is_nothrow_move_constructible<shmipc::Listener>::value,
              "listener must transfer ownership without throwing");
static_assert(!std::is_copy_constructible<shmipc::SessionManager>::value,
              "manager owns sessions and reconnect workers");
static_assert(
    std::is_nothrow_move_constructible<shmipc::SessionManager>::value,
    "manager must transfer ownership without throwing");
static_assert(!std::is_copy_constructible<shmipc::PooledStream>::value,
              "pooled stream is an exclusive lease");
static_assert(std::is_nothrow_move_constructible<shmipc::PooledStream>::value,
              "lease must transfer ownership without throwing");
static_assert(!std::is_copy_constructible<shmipc::StreamConnection>::value,
              "connection owns a stream");
static_assert(
    std::is_nothrow_move_constructible<shmipc::StreamConnection>::value,
    "connection must transfer ownership without throwing");

class Callbacks final : public shmipc::StreamCallbacks {
public:
    void on_data(shmipc::Stream&, std::vector<std::uint8_t>) override {}
};

class Monitor final : public shmipc::Monitor {
public:
    void on_session_metrics(const shmipc::SessionMetrics&) override {}
    shmipc::Status flush() override { return {}; }
};

class Logger final : public shmipc::Logger {
public:
    void log(shmipc::LogLevel, const std::string&,
             const std::string&) override {}
};

int main() {
    shmipc::ClientConfig config;
    shmipc::Session session;
    shmipc::Stream stream;
    shmipc::Listener listener;
    shmipc::ListenerConfig listener_config;
    shmipc::SessionManager manager;
    shmipc::SessionManagerConfig manager_config;
    shmipc::PooledStream pooled_stream;
    shmipc::StreamConnection connection;
    auto executor = std::make_shared<shmipc::CallbackExecutor>(1U);
    auto callbacks = std::make_shared<Callbacks>();
    auto monitor = std::make_shared<Monitor>();
    auto logger = std::make_shared<Logger>();
    config.monitor = monitor;
    config.logger = logger;
    const shmipc::SessionMetrics metrics{};
    shmipc::CallbackSubscription subscription;
    stream.set_read_deadline(shmipc::Stream::Clock::now() +
                             std::chrono::seconds(1));
    return shmipc::version.major == 0 && config.queue_capacity != 0U &&
                   executor->thread_count() == 1U && callbacks != nullptr &&
                   monitor != nullptr && logger != nullptr &&
                   metrics.protocol_version == 0U &&
                   std::string(shmipc::to_string(config.log_level)) ==
                       "warning" &&
                   listener_config.backlog > 0 && !subscription && !session &&
                   manager_config.session_count > 0U && !stream && !listener &&
                   !manager && !pooled_stream && !connection
               ? 0
               : 1;
}
