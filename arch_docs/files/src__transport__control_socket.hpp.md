# 文件 `src/transport/control_socket.hpp`

## Purpose

定义内部控制 socket 基础接口，为协议初始化阶段和后续 epoll event connection 提供统一的 FD ownership、Unix/TCP 建连与 exact IO 语义。

## Enums（全量）

### `TransportError`

- `none`
- `invalid_argument`
- `invalid_state`
- `end_of_stream`
- `would_block`
- `unsupported`
- `buffer_limit`
- `callback_error`
- `system_error`

## Structs and Aliases（全量）

- `TransportResult<T>`：`value`、分类错误和原始 `system_error`；支持显式成功布尔转换。
- `IoProgress`：本次调用已传输字节数。
- `IoResult = TransportResult<IoProgress>`。
- `ControlSocketResult = TransportResult<ControlSocket>`。
- `ControlListenerResult = TransportResult<ControlListener>`。

## Classes（全量）

- `ReceivedFileDescriptors`：move-only descriptor 集合；析构关闭所有未显式 `release(index)` 的 FD。
- `ControlSocket`：move-only FD owner；除基础生命周期和 exact IO 外，提供 `send_file_descriptors` 与 `receive_file_descriptors`。
- `ControlListener`：move-only listening FD owner；提供 `accept`、`local_port` 和 `close`。Unix listener 额外拥有已绑定路径的 unlink 责任。

## Functions（全量）

- `to_string(TransportError)`：稳定分类文本。
- `adopt_control_socket(fd)`：接管现有 FD 并配置 close-on-exec / no-SIGPIPE。
- `connect_tcp(host, port)`、`listen_tcp(host, port, backlog)`：通过 `getaddrinfo` 支持 IPv4/IPv6 候选，listener 的 port 0 可用于内核分配测试端口。
- `connect_unix(path)`、`listen_unix(path, backlog)`：使用 pathname Unix stream socket，并严格检查 `sun_path` 长度。

## Invariants & Gotchas

- adopt 无论成功失败都消费传入 FD；成功后只有 owner 或显式 `release()` 可转移所有权。
- exact IO 在 blocking socket 上循环到目标长度；nonblocking socket 返回 `would_block` 和已完成字节数，不隐式等待。
- `system_error` 对 syscall 错误保存 `errno`；名称解析失败保存 `getaddrinfo` 返回码。
- 关闭时先使对象失效再调用 `close`，不因 `EINTR` 重试可能已复用的 descriptor。
- Unix listen 不主动 unlink 未知已有路径，避免覆盖非本进程资源。

## Evidence

- 完整接口：`src/transport/control_socket.hpp:9-109`。
- FD 配置、ownership 与 IO：`src/transport/control_socket.cpp:18-176`。
- connect/listen/accept：`src/transport/control_socket.cpp:179-405`。
- 自动测试：`tests/control_socket_test.cpp:18-173`。

## Links

- [父目录](../dirs/src__transport.md)
- [架构决策](../02_DECISIONS.md)
- [回归测试指南](../../docs/regression-test-guide.md)
