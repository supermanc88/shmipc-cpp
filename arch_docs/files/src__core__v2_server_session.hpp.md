# 文件 `src/core/v2_server_session.hpp`

## Purpose

定义 v2 C++ server 的内部单 Session/单 Stream API。握手后 socket 移交 epoll；首个远端 opened queue element 动态绑定 Stream ID，随后提供消息级 receive/send、等待 Stream、双向 close 和 Session 关闭。

## API 与不变量

- `start_v2_server_session`：执行 server 文件路径握手并注册 dispatcher；mapper 不拥有共享文件 unlink 权。
- `wait_stream(timeout)`：等待首个非零 Stream ID、失败或 Session 关闭；真实 Go client 首个 ID 为 2。
- `receive/send`：每个共享 queue element 对应一条消息；producer 仅在 working `0→1` 时发送 Polling。
- `close_stream/wait_remote_close`：使用 closed queue element，亦接受控制通道 StreamClose；半关闭后的重复 close 幂等。
- 当前只绑定一个 ID；不同 ID 返回 `unexpected_stream`，多 Stream 由 `S-0305` 扩展。

## Evidence

- 实现：`src/core/v2_client_session.cpp` 中共享 callback state 与 server 方法。
- C++ peer：`tests/v2_server_session_test.cpp`，三条请求/响应、ID 2、timeout、跨 slice 和双向 close。
- Go peer：`TestV2ServerSessionInterop`，分别验证 C++ 主动 close 与 Go 主动 close。
- 远端 GCC 8.5 Debug/ASan 14/14，普通互操作 300/300，ASan helper 50/50；云端门禁待 push。

## Links

- [core 目录](../dirs/src__core.md)
- [client Session](src__core__v2_client_session.hpp.md)
- [buffer pool](src__shm__buffer_pool.hpp.md)
- [架构决策](../02_DECISIONS.md)
