# 文件 `src/transport/epoll_dispatcher.hpp`

## Purpose

定义 Linux 控制连接事件层：在握手完成后接管 `ControlSocket`，以 edge-triggered epoll 分发可消费读缓冲，并为并发写、关闭和 dispatcher 停止提供明确生命周期。

## Enums（全量）

### `ConnectionCloseReason`

- `local`
- `remote`
- `io_error`
- `buffer_limit`
- `callback_error`
- `dispatcher_shutdown`

## Structs and Aliases（全量）

- `ConsumeResult`：callback 已消费的前缀长度、分类错误及原始系统错误。
- `EpollDispatcherConfig`：读取块大小、单连接最大未消费字节和单轮最大事件数；默认分别为 64 KiB、64 MiB + 8 和 128。
- `EventConnectionResult`、`EpollDispatcherResult`：沿用 transport 分类错误的结果类型。

## Classes（全量）

- `ControlEventCallback`：`on_data` 消费缓冲前缀，`on_close` 接收唯一终态通知。
- `EventConnection`：不可复制/移动，由 `shared_ptr` 托管；提供线程安全的 `write`、`writev`、`close`、`is_open` 和只读 FD 查询。
- `EpollDispatcher`：move-only dispatcher handle；`add` 消费 socket，`stop` 唤醒并 join worker 后关闭余下连接。

## Functions（全量）

- `to_string(ConnectionCloseReason)`：稳定关闭原因文本。
- `start_epoll_dispatcher(config)`：Linux 创建 epoll/eventfd 和 worker；非 Linux 明确返回 `unsupported`。

## Invariants & Gotchas

- `add` 在注册 `EPOLLIN|EPOLLOUT|EPOLLET|EPOLLRDHUP` 前切换 nonblocking；调用后 socket 所有权不再属于调用者。
- ET 读循环持续到 `EAGAIN`。callback 只能消费当前可见前缀，未消费数据保留；非法长度、callback 错误/异常或超限会确定性关闭。
- 同一连接的 `write/writev` 整体串行；遇到背压时等待 EPOLLOUT generation，close 同时唤醒等待者。当前没有 deadline。
- FD syscall 与 close 由独立 mutex 互斥，避免 descriptor 关闭/复用竞态；atomic closed 保证 shutdown、注销和 `on_close` 至多一次。
- eventfd 的 read/write 重试 `EINTR` 并显式消费返回值，兼容启用 fortify 与 `-Werror` 的 GCC Release 构建。
- callback 按连接串行；`on_data` 内允许 `close()`，其 `on_close` 延迟到当前 callback 返回。不得从 dispatcher worker callback 内调用 `stop()`，该调用返回 `invalid_state`。
- `on_data` 通常运行于 worker；主动 close 的 `on_close` 可运行于调用线程，dispatcher shutdown 的 `on_close` 运行于 stop 调用线程，因此 callback 不应依赖固定线程亲和性。

## Evidence

- 完整接口：`src/transport/epoll_dispatcher.hpp:16-139`。
- eventfd helper：`src/transport/epoll_dispatcher.cpp:59-82`。
- 读写、回调与关闭：`src/transport/epoll_dispatcher.cpp:87-361`。
- 注册、事件循环和停止：`src/transport/epoll_dispatcher.cpp:363-595`。
- 自动测试：`tests/epoll_dispatcher_test.cpp:1-351`；远端 Linux GCC 8.5 Debug/ASan 11/11，专项连续 100 次通过。

## Links

- [父目录](../dirs/src__transport.md)
- [架构决策](../02_DECISIONS.md)
- [回归测试指南](../../docs/regression-test-guide.md)
