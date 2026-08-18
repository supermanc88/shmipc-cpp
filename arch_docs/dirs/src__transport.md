# 目录 `src/transport/`

## Summary

承载控制连接的 POSIX 系统调用边界。基础层提供 move-only Unix/TCP socket/listener、FD adoption 和 exact blocking IO；Linux 事件层提供 edge-triggered epoll、可消费读缓冲、串行并发写与确定性关闭通知。

## Directory Contents

| 路径 | 类型 | 状态 | 说明 |
|---|---|---|---|
| `control_socket.hpp` | 内部头文件 | ✅ | transport 错误、结果、socket/listener ownership 与工厂接口 |
| `control_socket.cpp` | C++ 实现 | ✅ | TCP/Unix connect/listen/accept、exact IO、FD flags 与路径清理 |
| `epoll_dispatcher.hpp` | 内部头文件 | ✅ | dispatcher/connection、消费回调、关闭原因与资源上限 |
| `epoll_dispatcher.cpp` | C++ 实现 | ✅ | epoll ET、eventfd 唤醒、读缓冲、写背压与关闭生命周期 |

## Invariants

- `ControlSocket` 和 `ControlListener` 不可复制、可移动，析构关闭唯一 owned FD；`adopt_control_socket` 从调用起接管 FD，即使配置失败也会关闭。
- 所有创建/采用/accept 的 FD 设置 `FD_CLOEXEC`；支持 `SO_NOSIGPIPE` 的平台设置该选项，其他平台写入使用 `MSG_NOSIGNAL`。
- `read_full/write_full` 重试 `EINTR`，保留 partial progress；EOF、would-block 与其他系统错误分类返回。
- Unix listener 不覆盖已有路径；仅在成功 bind 后拥有 unlink 责任，正常析构和 bind 后失败路径都会清理。
- 握手前使用 blocking exact IO；切换 epoll 前由调用方显式 `set_nonblocking(true)`，保持与固定 Go 实现相同的阶段边界。
- `EpollDispatcher::add` 消费 socket 并在注册前切换 nonblocking；连接由 dispatcher map 与调用者 `shared_ptr` 共同托管，close 只执行一次。
- 读事件必须排空到 `EAGAIN`；callback 返回消费前缀长度，未消费尾部保留到下一次事件。缓冲超过配置上限或 callback 违反消费契约时关闭连接。
- 每条连接的 `write/writev` 由 mutex 串行化；`EAGAIN` 等待 `EPOLLOUT` generation，close 会唤醒所有等待写，避免帧交错和永久阻塞。
- `on_close` 每条连接至多一次；`on_data` 内可重入 `close()`，通知延迟到当前 data callback 返回。dispatcher callback 线程不得同步 stop 自身。
- eventfd 唤醒读写显式检查 syscall 结果并重试 `EINTR`；nonblocking `EAGAIN` 表示已有唤醒计数或计数已被消费，不影响 stop 条件。

## Evidence

- Go blocking IO：`third_party/shmipc-go/block_io.go:25-54`。
- Go 从 duplicated FD 切换 dispatcher：`third_party/shmipc-go/session.go:121-177`、`event_dispatcher_linux.go:247-263`。
- C++ ownership/exact IO 与建连：`src/transport/control_socket.cpp:18-405`。
- C++ event connection、epoll loop 与启动/停止：`src/transport/epoll_dispatcher.cpp:68-595`。
- `tests/control_socket_test.cpp:18-173` 覆盖 partial read、EOF、would-block、TCP、Unix、重复 bind、路径清理和错误；本机 Debug/ASan+UBSan/TSan、远端 GCC 8.5 Debug/ASan 各 10/10 通过。
- `tests/epoll_dispatcher_test.cpp:1-351` 覆盖 partial frame、writev、EAGAIN 背压、并发写无交错、remote/local/shutdown close、缓冲上限、callback 错误/重入 close 和非法配置；远端 GCC 8.5 Debug/ASan 各 11/11，专项连续 100 次通过。

## Guesses & Uncertainties

- Linux TSan 需等待本次提交推送后的 GitHub Actions；远端仅安装 ASan runtime。
- 本层不提供握手或事件写 deadline；上层 Session/Stream 需统一定义超时和取消策略后再扩展，避免在 blocking helper 与 dispatcher 内形成两套等待模型。

## Links

- [架构概要](../01_OVERVIEW.md)
- [决策与风险](../02_DECISIONS.md)
- [control socket 文件](../files/src__transport__control_socket.hpp.md)
- [epoll dispatcher 文件](../files/src__transport__epoll_dispatcher.hpp.md)
- [回归测试指南](../../docs/regression-test-guide.md)
