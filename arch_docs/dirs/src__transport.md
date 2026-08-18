# 目录 `src/transport/`

## Summary

承载控制连接的 POSIX 系统调用边界。当前基础层提供 move-only Unix/TCP socket/listener、FD adoption、exact blocking IO 和 nonblocking 切换；Linux epoll dispatcher 仍是 `S-0301` 的下一子切片。

## Directory Contents

| 路径 | 类型 | 状态 | 说明 |
|---|---|---|---|
| `control_socket.hpp` | 内部头文件 | ✅ | transport 错误、结果、socket/listener ownership 与工厂接口 |
| `control_socket.cpp` | C++ 实现 | ✅ | TCP/Unix connect/listen/accept、exact IO、FD flags 与路径清理 |

## Invariants

- `ControlSocket` 和 `ControlListener` 不可复制、可移动，析构关闭唯一 owned FD；`adopt_control_socket` 从调用起接管 FD，即使配置失败也会关闭。
- 所有创建/采用/accept 的 FD 设置 `FD_CLOEXEC`；支持 `SO_NOSIGPIPE` 的平台设置该选项，其他平台写入使用 `MSG_NOSIGNAL`。
- `read_full/write_full` 重试 `EINTR`，保留 partial progress；EOF、would-block 与其他系统错误分类返回。
- Unix listener 不覆盖已有路径；仅在成功 bind 后拥有 unlink 责任，正常析构和 bind 后失败路径都会清理。
- 握手前使用 blocking exact IO；切换 epoll 前由调用方显式 `set_nonblocking(true)`，保持与固定 Go 实现相同的阶段边界。

## Evidence

- Go blocking IO：`third_party/shmipc-go/block_io.go:25-54`。
- Go 从 duplicated FD 切换 dispatcher：`third_party/shmipc-go/session.go:121-177`、`event_dispatcher_linux.go:247-263`。
- C++ ownership/exact IO：`src/transport/control_socket.cpp:18-176`。
- TCP/Unix listener 与连接：`src/transport/control_socket.cpp:179-402`。
- `tests/control_socket_test.cpp:18-173` 覆盖 partial read、EOF、would-block、TCP、Unix、重复 bind、路径清理和错误；本机 Debug/ASan+UBSan/TSan、远端 GCC 8.5 Debug/ASan 各 10/10 通过。

## Guesses & Uncertainties

- 当前未实现连接 deadline；Go 初始化阶段也直接使用 raw FD exact IO。若后续握手需要硬超时，应由 poll/dispatcher 或上层 deadline 统一提供，避免在 blocking helper 内引入第二套等待模型。
- epoll ET 的缓冲消费、并发写串行化与 close callback 生命周期待下一子切片验证。

## Links

- [架构概要](../01_OVERVIEW.md)
- [决策与风险](../02_DECISIONS.md)
- [control socket 文件](../files/src__transport__control_socket.hpp.md)
- [回归测试指南](../../docs/regression-test-guide.md)
