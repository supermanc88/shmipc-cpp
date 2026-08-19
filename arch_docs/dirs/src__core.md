# 目录 `src/core/`

## Summary

组合 protocol、transport 与 shm 子系统形成可执行的会话初始化和数据路径。当前实现 v2 文件路径握手、完整 v3 memfd 资源握手、client/server 单 Stream 基线，以及带 deadline、queue-full retry 和错误扇出的 client-originated 多 Stream Session；v3 Session 数据接入与数据 fallback 仍由后续切片补充。

## Directory Contents

| 路径 | 类型 | 状态 | 说明 |
|---|---|---|---|
| `v2_handshake.hpp` | 内部头文件 | ✅ | v2 配置、结果、细分错误与共享资源 ownership |
| `v2_handshake.cpp` | C++ 实现 | ✅ | client 创建并发送 metadata；server 接收并映射资源 |
| `protocol_version_negotiation.hpp` | 内部头文件 | ✅ | v2/v3 版本边界、双角色结果与错误模型 |
| `protocol_version_negotiation.cpp` | C++ 实现 | ✅ | v3 offer/response、v2 降级选择与首帧校验 |
| `v3_handshake.hpp` | 内部头文件 | ✅ | v3 配置、共享资源、细分错误与 ownership |
| `v3_handshake.cpp` | C++ 实现 | ✅ | memfd metadata、ACK、FD 传递、映射和 queue 方向 |
| `v2_client_session.hpp` | 内部头文件 | ✅ | 单 client Session/Stream API、状态与错误模型 |
| `v2_client_session.cpp` | C++ 实现 | ✅ | epoll callback、queue/buffer 数据面、Polling 与关闭 |
| `v2_server_session.hpp` | 内部头文件 | ✅ | server 动态绑定首个远端 Stream、收发与关闭 API |
| `v2_multiplexed_session.hpp` | 内部头文件 | ✅ | 多路 client/server Session 与独立 Stream 句柄 |
| `v2_multiplexed_session.cpp` | C++ 实现 | ✅ | 连接级路由/accept、per-Stream 消息/关闭/deadline 与 queue-full retry |

## Invariants

- v2 文件模式不做版本协商或 ACK；客户端只发送一帧 version 2 `share_memory_by_file_path` metadata。
- v3 client 固定宣告最高版本 3，接收 peer maximum 后取较小值；结果 2 是合法降级，结果 3 才能继续 memfd。
- v3 server 只接受 length 8、version 3 的 `ExchangeProtoVersion` 首帧，并始终回复本地最高版本 3；不能接受 version 2 Exchange 帧。
- v3 descriptor 顺序固定为 `[buffer_fd, queue_fd]`；server 必须恰好接收两个 FD，并只在两个映射、pool 和 queue 全部就绪后发送 `AckShareMemory`。
- v3 metadata 中的名称是逻辑标识，不用于打开路径；queue 前后半区在 creator/mapper 两个角色间反向映射。
- client 是 buffer/queue 文件 creator，物理 queue 前半段为 send、后半段为 receive；server 映射后方向反转。
- creator 独占路径 unlink 责任，mapper 只持有 mapping，避免 server 误删不属于自己的路径。
- 握手复用 blocking `ControlSocket::read_full/write_full`；成功后 socket 仍由调用者拥有，便于 Session 将其移动到 epoll dispatcher。
- 任一步骤失败时，已创建的文件和 mapping 通过 RAII 回滚；已存在的非本进程文件不会被删除。
- 单 Stream 基线仍分别固定 ID 1/动态绑定一个 ID；多路 client 按固定 Go 源码从 2 连续分配，server 在每个未知 opened ID 的首包到达时 Accept。
- 多路实现只支持 client 主动开流，不承诺固定 Go 尚未实现的双向开流。
- 多路 Stream 实现 persistent deadline 与 queue-full retry；仅 close 使用控制通道 fallback，数据 fallback 和 Session 初始化超时仍由后续切片处理。

## Evidence

- 上游事件序列：`third_party/shmipc-go/protocol_initializer.go:52-65`、`protocol_manager.go:75-149`、`session.go:125-179`。
- queue 创建/映射方向：`third_party/shmipc-go/queue.go:88-176`、`216-233`。
- buffer 创建/映射：`third_party/shmipc-go/buffer_manager.go:182-245`、`573-602`。
- `tests/v2_handshake_test.cpp` 覆盖成功、双向 queue、两角色 buffer、错误版本/事件、截断、缺失路径、已有文件保护及失败清理。
- `tests/protocol_version_negotiation_test.cpp` 覆盖 v3 成功、v2 降级、未来版本、错误 length/type/magic、低版本、EOF 与角色差异；固定 Go oracle 双向 20 轮通过，远端 GCC 8.5 Debug/ASan 16/16。
- `tests/v3_handshake_test.cpp` 覆盖完整资源握手、错误 ACK、FD 数量、截断 metadata、异常回滚与 `/proc/self/fd` 泄漏检查；固定 Go `newSession` 双向普通 100 轮、ASan helper 20 轮通过。
- 固定 Go overlay 在远端 Linux 验证两个方向，并连续重复 50 轮；GCC 8.5 Debug/ASan 与提交 `3f2db07` 的 run `32151993614` 七项门禁通过。
- `tests/v2_client_session_test.cpp` 与 Go oracle 验证 C++ client→Go server 的 20,000/17,000 字节双向链、Polling、timeout 和 close；远端 Debug/ASan、50/50 重复及提交 `050d7da` 的 run `32154121843` 七项门禁通过。
- `tests/v2_server_session_test.cpp` 与 Go oracle 验证 Go client→C++ server 的三消息双向链、ID 2 动态绑定、批量 Polling 和两个方向 close；远端 Debug/ASan 14/14、普通 300/300、ASan 50/50 及提交 `0347f34` 的 run `32158446306` 七项门禁通过。
- `tests/v2_multiplexed_session_test.cpp` 与双向 Go oracle 验证 ID 2/3/4、并发首包、server Accept、独立收发、deadline、queue-full retry/close fallback、错误扇出与无 ACK close；本地三套 sanitizer、远端 Debug/ASan、普通互操作 100 轮及 ASan 20 轮通过。
- 提交 `78913e6` 的 GitHub Actions run `32204938990` 七项门禁全部成功，Go protocol oracle CTest 为 16/16；`S-0305a/b` 与 M3 正式关闭。

## Links

- [v2 handshake 文件](../files/src__core__v2_handshake.hpp.md)
- [版本协商文件](../files/src__core__protocol_version_negotiation.hpp.md)
- [v3 handshake 文件](../files/src__core__v3_handshake.hpp.md)
- [v2 client Session 文件](../files/src__core__v2_client_session.hpp.md)
- [v2 server Session 文件](../files/src__core__v2_server_session.hpp.md)
- [v2 multiplexed Session 文件](../files/src__core__v2_multiplexed_session.hpp.md)
- [架构概要](../01_OVERVIEW.md)
- [回归测试指南](../../docs/regression-test-guide.md)
