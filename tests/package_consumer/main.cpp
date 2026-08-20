#include <shmipc/session.hpp>
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

class Callbacks final : public shmipc::StreamCallbacks {
public:
    void on_data(shmipc::Stream&, std::vector<std::uint8_t>) override {}
};

int main() {
    shmipc::ClientConfig config;
    shmipc::Session session;
    shmipc::Stream stream;
    auto executor = std::make_shared<shmipc::CallbackExecutor>(1U);
    auto callbacks = std::make_shared<Callbacks>();
    shmipc::CallbackSubscription subscription;
    stream.set_read_deadline(shmipc::Stream::Clock::now() +
                             std::chrono::seconds(1));
    return shmipc::version.major == 0 && config.queue_capacity != 0U &&
                   executor->thread_count() == 1U && callbacks != nullptr &&
                   !subscription && !session && !stream
               ? 0
               : 1;
}
