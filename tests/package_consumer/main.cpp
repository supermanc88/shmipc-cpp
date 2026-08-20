#include <shmipc/session.hpp>
#include <shmipc/version.hpp>

#include <chrono>
#include <type_traits>

static_assert(!std::is_copy_constructible<shmipc::Session>::value, "RAII owner");
static_assert(std::is_nothrow_move_constructible<shmipc::Session>::value,
              "session must transfer ownership without throwing");
static_assert(!std::is_copy_constructible<shmipc::Stream>::value, "RAII owner");
static_assert(std::is_nothrow_move_constructible<shmipc::Stream>::value,
              "stream must transfer ownership without throwing");

int main() {
    shmipc::ClientConfig config;
    shmipc::Session session;
    shmipc::Stream stream;
    stream.set_read_deadline(shmipc::Stream::Clock::now() +
                             std::chrono::seconds(1));
    return shmipc::version.major == 0 && config.queue_capacity != 0U &&
                   !session && !stream
               ? 0
               : 1;
}
