# 目录 `src/core/`

## Summary

组合 protocol、transport 与 shm 子系统形成可执行的会话初始化和最小数据路径。当前实现 v2 文件路径握手，以及 client/server 两个角色的单 Session/单 Stream；多 Stream、v3 与 fallback 仍由后续切片补充。

## Directory Contents

| 路径 | 类型 | 状态 | 说明 |
|---|---|---|---|
| `v2_handshake.hpp` | 内部头文件 | ✅ | v2 配置、结果、细分错误与共享资源 ownership |
| `v2_handshake.cpp` | C++ 实现 | ✅ | client 创建并发送 metadata；server 接收并映射资源 |
| `v2_client_session.hpp` | 内部头文件 | ✅ | 单 client Session/Stream API、状态与错误模型 |
| `v2_client_session.cpp` | C++ 实现 | ✅ | epoll callback、queue/buffer 数据面、Polling 与关闭 |
| `v2_server_session.hpp` | 内部头文件 | ✅ | server 动态绑定首个远端 Stream、收发与关闭 API |

## Invariants

- v2 文件模式不做版本协商或 ACK；客户端只发送一帧 version 2 `share_memory_by_file_path` metadata。
- client 是 buffer/queue 文件 creator，物理 queue 前半段为 send、后半段为 receive；server 映射后方向反转。
- creator 独占路径 unlink 责任，mapper 只持有 mapping，避免 server 误删不属于自己的路径。
- 握手复用 blocking `ControlSocket::read_full/write_full`；成功后 socket 仍由调用者拥有，便于 Session 将其移动到 epoll dispatcher。
- 任一步骤失败时，已创建的文件和 mapping 通过 RAII 回滚；已存在的非本进程文件不会被删除。
- 本层不实现 deadline；Session 初始化超时需要在后续集成层统一取消 socket 阻塞。
- client 当前固定 Stream ID 1；server 从首个远端 opened element 动态绑定非零 ID（真实 Go client 首个为 2）。两者都不静默执行 fallback 或多 Stream。

## Evidence

- 上游事件序列：`third_party/shmipc-go/protocol_initializer.go:52-65`、`protocol_manager.go:75-149`、`session.go:125-179`。
- queue 创建/映射方向：`third_party/shmipc-go/queue.go:88-176`、`216-233`。
- buffer 创建/映射：`third_party/shmipc-go/buffer_manager.go:182-245`、`573-602`。
- `tests/v2_handshake_test.cpp` 覆盖成功、双向 queue、两角色 buffer、错误版本/事件、截断、缺失路径、已有文件保护及失败清理。
- 固定 Go overlay 在远端 Linux 验证两个方向，并连续重复 50 轮；GCC 8.5 Debug/ASan 与提交 `3f2db07` 的 run `32151993614` 七项门禁通过。
- `tests/v2_client_session_test.cpp` 与 Go oracle 验证 C++ client→Go server 的 20,000/17,000 字节双向链、Polling、timeout 和 close；远端 Debug/ASan、50/50 重复及提交 `050d7da` 的 run `32154121843` 七项门禁通过。
- `tests/v2_server_session_test.cpp` 与 Go oracle 验证 Go client→C++ server 的三消息双向链、ID 2 动态绑定、批量 Polling 和两个方向 close；远端 Debug/ASan 14/14、普通 300/300 与 ASan 50/50 通过。

## Links

- [v2 handshake 文件](../files/src__core__v2_handshake.hpp.md)
- [v2 client Session 文件](../files/src__core__v2_client_session.hpp.md)
- [v2 server Session 文件](../files/src__core__v2_server_session.hpp.md)
- [架构概要](../01_OVERVIEW.md)
- [回归测试指南](../../docs/regression-test-guide.md)
