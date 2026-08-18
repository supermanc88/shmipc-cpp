#include "transport/epoll_dispatcher.hpp"

#include <cerrno>
#include <cstring>
#include <thread>
#include <unordered_map>
#include <utility>

#include <sys/socket.h>
#include <unistd.h>

#ifdef __linux__
#include <sys/epoll.h>
#include <sys/eventfd.h>
#endif

namespace shmipc::transport {

struct EpollState final : std::enable_shared_from_this<EpollState> {
    explicit EpollState(EpollDispatcherConfig value)
        : config(value)
#ifdef __linux__
          ,
          events(static_cast<std::size_t>(value.max_events))
#endif
    {}

    EpollDispatcherConfig config{};
    std::mutex connections_mutex{};
    std::unordered_map<int, std::shared_ptr<EventConnection>> connections{};
    std::atomic<bool> stopping{false};
    std::thread worker{};
    int epoll_fd{-1};
    int wake_fd{-1};
#ifdef __linux__
    std::vector<epoll_event> events{};
#endif

    [[nodiscard]] EventConnectionResult add(
        ControlSocket&& socket,
        std::shared_ptr<ControlEventCallback> callback) noexcept;
    void remove(int fd) noexcept;
    void run() noexcept;
    [[nodiscard]] TransportError stop() noexcept;
};

namespace {

thread_local EventConnection* active_callback = nullptr;

int event_send_flags() noexcept {
#ifdef MSG_NOSIGNAL
    return MSG_NOSIGNAL;
#else
    return 0;
#endif
}

#ifdef __linux__
void close_descriptor(int& fd) noexcept {
    if (fd >= 0) {
        const auto owned = fd;
        fd = -1;
        static_cast<void>(::close(owned));
    }
}

void consume_wake(int fd) noexcept {
    std::uint64_t value = 0;
    ssize_t result = -1;
    do {
        result = ::read(fd, &value, sizeof(value));
    } while (result < 0 && errno == EINTR);
}

void signal_wake(int fd) noexcept {
    const std::uint64_t value = 1;
    ssize_t result = -1;
    do {
        result = ::write(fd, &value, sizeof(value));
    } while (result < 0 && errno == EINTR);
}
#endif

}  // namespace

EventConnection::EventConnection(
    ControlSocket&& socket, std::shared_ptr<ControlEventCallback> callback,
    std::weak_ptr<EpollState> dispatcher, std::size_t read_chunk_size,
    std::size_t max_buffered_bytes)
    : socket_(std::move(socket)),
      native_fd_(socket_.native_handle()),
      callback_(std::move(callback)),
      dispatcher_(std::move(dispatcher)),
      read_chunk_size_(read_chunk_size),
      max_buffered_bytes_(max_buffered_bytes) {
    read_buffer_.reserve(read_chunk_size_);
}

EventConnection::~EventConnection() {
    close_internal(ConnectionCloseReason::local, 0, true);
}

bool EventConnection::is_open() const noexcept {
    return !closed_.load(std::memory_order_acquire);
}

int EventConnection::native_handle() const noexcept {
    return is_open() ? native_fd_ : -1;
}

IoResult EventConnection::write_locked(const std::uint8_t* data,
                                       std::size_t size) noexcept {
    if (data == nullptr && size != 0U) {
        return {{0U}, TransportError::invalid_argument, EINVAL};
    }
    if (!is_open()) {
        return {{0U}, TransportError::invalid_state, EBADF};
    }
    std::size_t transferred = 0;
    while (transferred < size) {
        std::uint64_t generation = 0;
        {
            std::lock_guard<std::mutex> lock(writable_mutex_);
            generation = writable_generation_;
        }
        if (!is_open()) {
            return {{transferred}, TransportError::invalid_state, EBADF};
        }
        ssize_t count = -1;
        int saved_error = 0;
        {
            std::lock_guard<std::mutex> fd_lock(fd_mutex_);
            if (!is_open()) {
                return {{transferred}, TransportError::invalid_state, EBADF};
            }
            count = ::send(native_fd_, data + transferred, size - transferred,
                           event_send_flags());
            saved_error = errno;
        }
        if (count > 0) {
            transferred += static_cast<std::size_t>(count);
            continue;
        }
        if (count == 0) {
            return {{transferred}, TransportError::system_error, EIO};
        }
        if (saved_error == EINTR) {
            continue;
        }
        if (saved_error != EAGAIN && saved_error != EWOULDBLOCK) {
            return {{transferred}, TransportError::system_error, saved_error};
        }
        std::unique_lock<std::mutex> lock(writable_mutex_);
        writable_cv_.wait(lock, [&] {
            return !is_open() || writable_generation_ != generation;
        });
    }
    return {{transferred}, TransportError::none, 0};
}

IoResult EventConnection::write(const std::uint8_t* data,
                                std::size_t size) noexcept {
    std::lock_guard<std::mutex> lock(write_mutex_);
    return write_locked(data, size);
}

IoResult EventConnection::writev(
    const std::vector<std::pair<const std::uint8_t*, std::size_t>>& buffers)
    noexcept {
    std::lock_guard<std::mutex> lock(write_mutex_);
    std::size_t transferred = 0;
    for (const auto& buffer : buffers) {
        const auto result = write_locked(buffer.first, buffer.second);
        transferred += result.value.transferred;
        if (!result) {
            return {{transferred}, result.error, result.system_error};
        }
    }
    return {{transferred}, TransportError::none, 0};
}

TransportError EventConnection::close() noexcept {
    close_internal(ConnectionCloseReason::local, 0, true);
    return TransportError::none;
}

void EventConnection::notify_writable() noexcept {
    {
        std::lock_guard<std::mutex> lock(writable_mutex_);
        ++writable_generation_;
    }
    writable_cv_.notify_all();
}

void EventConnection::handle_read() noexcept {
    std::vector<std::uint8_t> chunk;
    try {
        chunk.resize(read_chunk_size_);
    } catch (...) {
        close_internal(ConnectionCloseReason::io_error, ENOMEM, true);
        return;
    }
    bool remote_eof = false;
    for (;;) {
        ssize_t count = -1;
        int saved_error = 0;
        {
            std::lock_guard<std::mutex> fd_lock(fd_mutex_);
            if (!is_open()) {
                return;
            }
            count = ::recv(native_fd_, chunk.data(), chunk.size(), 0);
            saved_error = errno;
        }
        if (count > 0) {
            const auto amount = static_cast<std::size_t>(count);
            const auto buffered = read_buffer_.size() - read_offset_;
            if (amount > max_buffered_bytes_ - buffered) {
                close_internal(ConnectionCloseReason::buffer_limit, EMSGSIZE,
                               true);
                return;
            }
            try {
                read_buffer_.insert(read_buffer_.end(), chunk.data(),
                                    chunk.data() + amount);
            } catch (...) {
                close_internal(ConnectionCloseReason::io_error, ENOMEM, true);
                return;
            }
            continue;
        }
        if (count == 0) {
            remote_eof = true;
            break;
        }
        if (saved_error == EINTR) {
            continue;
        }
        if (saved_error != EAGAIN && saved_error != EWOULDBLOCK) {
            close_internal(ConnectionCloseReason::io_error, saved_error, true);
            return;
        }
        break;
    }

    const auto available = read_buffer_.size() - read_offset_;
    if (available == 0U || !is_open()) {
        if (remote_eof && is_open()) {
            close_internal(ConnectionCloseReason::remote, 0, true);
        }
        return;
    }
    ConsumeResult result{};
    try {
        std::lock_guard<std::recursive_mutex> lock(callback_mutex_);
        if (!is_open()) {
            return;
        }
        active_callback = this;
        result = callback_->on_data(read_buffer_.data() + read_offset_,
                                    available, *this);
        active_callback = nullptr;
        if (pending_close_callback_) {
            pending_close_callback_ = false;
            callback_->on_close(pending_close_reason_, pending_close_error_);
        }
    } catch (...) {
        active_callback = nullptr;
        close_internal(ConnectionCloseReason::callback_error, EFAULT, true);
        return;
    }
    if (!is_open()) {
        return;
    }
    if (result.error != TransportError::none || result.consumed > available) {
        close_internal(ConnectionCloseReason::callback_error,
                       result.system_error == 0 ? EPROTO : result.system_error,
                       true);
        return;
    }
    read_offset_ += result.consumed;
    if (read_offset_ == read_buffer_.size()) {
        read_buffer_.clear();
        read_offset_ = 0;
    } else if (read_offset_ >= read_chunk_size_) {
        read_buffer_.erase(read_buffer_.begin(),
                           read_buffer_.begin() +
                               static_cast<std::ptrdiff_t>(read_offset_));
        read_offset_ = 0;
    }
    if (remote_eof) {
        close_internal(ConnectionCloseReason::remote, 0, true);
    }
}

void EventConnection::handle_events(std::uint32_t events) noexcept {
#ifdef __linux__
    if ((events & EPOLLIN) != 0U) {
        handle_read();
    }
    if (!is_open()) {
        return;
    }
    if ((events & EPOLLOUT) != 0U) {
        notify_writable();
    }
    if ((events & EPOLLERR) != 0U) {
        int socket_error = 0;
        socklen_t size = sizeof(socket_error);
        {
            std::lock_guard<std::mutex> fd_lock(fd_mutex_);
            if (!is_open()) {
                return;
            }
            if (::getsockopt(native_fd_, SOL_SOCKET, SO_ERROR, &socket_error,
                             &size) != 0) {
                socket_error = errno;
            }
        }
        close_internal(ConnectionCloseReason::io_error, socket_error, true);
        return;
    }
    if ((events & (EPOLLRDHUP | EPOLLHUP)) != 0U) {
        close_internal(ConnectionCloseReason::remote, 0, true);
    }
#else
    static_cast<void>(events);
#endif
}

void EventConnection::close_internal(ConnectionCloseReason reason,
                                     int system_error,
                                     bool unregister) noexcept {
    if (closed_.exchange(true, std::memory_order_acq_rel)) {
        return;
    }
    const auto fd = native_fd_;
    notify_writable();
    {
        std::lock_guard<std::mutex> fd_lock(fd_mutex_);
        static_cast<void>(socket_.shutdown());
        static_cast<void>(socket_.close());
    }
    if (unregister) {
        if (auto state = dispatcher_.lock()) {
            state->remove(fd);
        }
    }
    if (active_callback == this) {
        pending_close_callback_ = true;
        pending_close_reason_ = reason;
        pending_close_error_ = system_error;
        return;
    }
    try {
        std::lock_guard<std::recursive_mutex> lock(callback_mutex_);
        callback_->on_close(reason, system_error);
    } catch (...) {
    }
}

EventConnectionResult EpollState::add(
    ControlSocket&& socket,
    std::shared_ptr<ControlEventCallback> callback) noexcept {
#ifdef __linux__
    if (stopping.load(std::memory_order_acquire)) {
        return {{}, TransportError::invalid_state, ECANCELED};
    }
    if (!socket || !callback) {
        return {{}, TransportError::invalid_argument, EINVAL};
    }
    if (socket.set_nonblocking(true) != TransportError::none) {
        return {{}, TransportError::system_error, errno};
    }
    std::shared_ptr<EventConnection> connection;
    try {
        connection = std::shared_ptr<EventConnection>(new EventConnection(
            std::move(socket), std::move(callback), weak_from_this(),
            config.read_chunk_size, config.max_buffered_bytes));
    } catch (...) {
        return {{}, TransportError::system_error, ENOMEM};
    }
    const auto fd = connection->native_handle();
    epoll_event event{};
    event.events = EPOLLIN | EPOLLOUT | EPOLLET | EPOLLRDHUP;
    event.data.fd = fd;
    {
        std::lock_guard<std::mutex> lock(connections_mutex);
        if (stopping.load(std::memory_order_acquire)) {
            return {{}, TransportError::invalid_state, ECANCELED};
        }
        if (connections.find(fd) != connections.end()) {
            return {{}, TransportError::invalid_state, EEXIST};
        }
        connections.emplace(fd, connection);
        if (::epoll_ctl(epoll_fd, EPOLL_CTL_ADD, fd, &event) != 0) {
            const auto saved = errno;
            connections.erase(fd);
            return {{}, TransportError::system_error, saved};
        }
    }
    return {std::move(connection), TransportError::none, 0};
#else
    static_cast<void>(socket);
    static_cast<void>(callback);
    return {{}, TransportError::unsupported, ENOTSUP};
#endif
}

void EpollState::remove(int fd) noexcept {
#ifdef __linux__
    if (fd < 0) {
        return;
    }
    static_cast<void>(::epoll_ctl(epoll_fd, EPOLL_CTL_DEL, fd, nullptr));
    std::lock_guard<std::mutex> lock(connections_mutex);
    connections.erase(fd);
#else
    static_cast<void>(fd);
#endif
}

void EpollState::run() noexcept {
#ifdef __linux__
    while (!stopping.load(std::memory_order_acquire)) {
        const auto count = ::epoll_wait(epoll_fd, events.data(),
                                        static_cast<int>(events.size()), -1);
        if (count < 0) {
            if (errno == EINTR) {
                continue;
            }
            break;
        }
        for (int index = 0; index < count; ++index) {
            const auto fd = events[static_cast<std::size_t>(index)].data.fd;
            if (fd == wake_fd) {
                consume_wake(wake_fd);
                continue;
            }
            std::shared_ptr<EventConnection> connection;
            {
                std::lock_guard<std::mutex> lock(connections_mutex);
                const auto found = connections.find(fd);
                if (found != connections.end()) {
                    connection = found->second;
                }
            }
            if (connection) {
                connection->handle_events(
                    events[static_cast<std::size_t>(index)].events);
            }
        }
    }
#endif
}

TransportError EpollState::stop() noexcept {
    if (worker.joinable() && worker.get_id() == std::this_thread::get_id()) {
        return TransportError::invalid_state;
    }
    if (stopping.exchange(true, std::memory_order_acq_rel)) {
        return TransportError::none;
    }
#ifdef __linux__
    if (wake_fd >= 0) {
        signal_wake(wake_fd);
    }
    if (worker.joinable()) {
        worker.join();
    }
    std::vector<std::shared_ptr<EventConnection>> closing;
    {
        std::lock_guard<std::mutex> lock(connections_mutex);
        closing.reserve(connections.size());
        for (auto& entry : connections) {
            closing.push_back(std::move(entry.second));
        }
        connections.clear();
    }
    for (auto& connection : closing) {
        connection->close_internal(ConnectionCloseReason::dispatcher_shutdown,
                                   0, false);
    }
    close_descriptor(wake_fd);
    close_descriptor(epoll_fd);
#endif
    return TransportError::none;
}

EpollDispatcher::EpollDispatcher(std::shared_ptr<EpollState> state) noexcept
    : state_(std::move(state)) {}

EpollDispatcher::~EpollDispatcher() { static_cast<void>(stop()); }

EpollDispatcher::EpollDispatcher(EpollDispatcher&& other) noexcept
    : state_(std::move(other.state_)) {}

EpollDispatcher& EpollDispatcher::operator=(EpollDispatcher&& other) noexcept {
    if (this != &other) {
        static_cast<void>(stop());
        state_ = std::move(other.state_);
    }
    return *this;
}

EpollDispatcher::operator bool() const noexcept {
    return state_ != nullptr &&
           !state_->stopping.load(std::memory_order_acquire);
}

EventConnectionResult EpollDispatcher::add(
    ControlSocket&& socket,
    std::shared_ptr<ControlEventCallback> callback) noexcept {
    if (!state_) {
        return {{}, TransportError::invalid_state, EBADF};
    }
    return state_->add(std::move(socket), std::move(callback));
}

TransportError EpollDispatcher::stop() noexcept {
    if (!state_) {
        return TransportError::none;
    }
    const auto result = state_->stop();
    if (result == TransportError::none) {
        state_.reset();
    }
    return result;
}

const char* to_string(ConnectionCloseReason reason) noexcept {
    switch (reason) {
        case ConnectionCloseReason::local:
            return "local";
        case ConnectionCloseReason::remote:
            return "remote";
        case ConnectionCloseReason::io_error:
            return "IO error";
        case ConnectionCloseReason::buffer_limit:
            return "buffer limit";
        case ConnectionCloseReason::callback_error:
            return "callback error";
        case ConnectionCloseReason::dispatcher_shutdown:
            return "dispatcher shutdown";
    }
    return "unknown close reason";
}

EpollDispatcherResult start_epoll_dispatcher(
    const EpollDispatcherConfig& config) {
#ifdef __linux__
    if (config.read_chunk_size == 0U ||
        config.max_buffered_bytes < config.read_chunk_size ||
        config.max_events <= 0 || config.max_events > 4096) {
        return {{}, TransportError::invalid_argument, EINVAL};
    }
    std::shared_ptr<EpollState> state;
    try {
        state = std::make_shared<EpollState>(config);
    } catch (...) {
        return {{}, TransportError::system_error, ENOMEM};
    }
    state->epoll_fd = ::epoll_create1(EPOLL_CLOEXEC);
    if (state->epoll_fd < 0) {
        return {{}, TransportError::system_error, errno};
    }
    state->wake_fd = ::eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK);
    if (state->wake_fd < 0) {
        const auto saved = errno;
        close_descriptor(state->epoll_fd);
        return {{}, TransportError::system_error, saved};
    }
    epoll_event wake_event{};
    wake_event.events = EPOLLIN;
    wake_event.data.fd = state->wake_fd;
    if (::epoll_ctl(state->epoll_fd, EPOLL_CTL_ADD, state->wake_fd,
                    &wake_event) != 0) {
        const auto saved = errno;
        close_descriptor(state->wake_fd);
        close_descriptor(state->epoll_fd);
        return {{}, TransportError::system_error, saved};
    }
    try {
        state->worker = std::thread([state] { state->run(); });
    } catch (...) {
        close_descriptor(state->wake_fd);
        close_descriptor(state->epoll_fd);
        return {{}, TransportError::system_error, EAGAIN};
    }
    return {EpollDispatcher(std::move(state)), TransportError::none, 0};
#else
    static_cast<void>(config);
    return {{}, TransportError::unsupported, ENOTSUP};
#endif
}

}  // namespace shmipc::transport
