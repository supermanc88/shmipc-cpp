# 文件 `src/core/v2_client_session.hpp`

## Purpose

定义 v2 client 的最小单 Session/单 Stream 数据面：握手后将 control socket 移入 epoll，通过共享 buffer/queue 发送和接收消息，并处理 Polling 与 StreamClose。

## Types（全量）

- `V2SessionError`：参数、握手、dispatcher、transport、codec、事件/stream、queue、pool、Buffer IO、Session unhealthy、关闭与超时分类；多路 Session 使用 unhealthy 表示 breaker 拒绝新流。
- `V2SessionStatus`：保留握手和各底层子系统错误。
- `V2ClientSession`：move-only Session owner；当前固定 Stream ID 1，提供 `send`、`receive`、`close_stream`、`wait_remote_close` 与 `close`。
- `V2ClientSession::MessageResult`：一条完整共享内存消息及状态。
- `V2ClientSessionResult`：Session 初始化结果。

## Control Flow

1. `start_v2_client_session` 在 blocking socket 上执行 v2 client handshake。
2. 成功后把 socket 和 callback 注册到 `EpollDispatcher`。
3. `send` 使用 `BufferWriter` publish chain，写 queue element `{1, root, opened}`；send queue working 从 0 变 1 时写 8 字节 Polling。
4. callback 收到 Polling 后循环 pop receive queue；opened element 通过 `adopt_chain + BufferReader` 形成消息并回收，closed element 发布远端关闭状态。
5. `close_stream` 写 `{1, 0, closed}`；控制通道 StreamClose 也作为 queue-full close 的兼容接收路径。

## Invariants & Boundaries

- 此早期单 Stream 基线固定 ID 1，真实 Go server 可接受该 ID；它不复现固定 Go client 从 2 开始的 allocator，多路兼容路径见 `v2_multiplexed_session.hpp`。
- 消息边界等于一个 queue element 对应的一条 buffer chain。
- queue working 的清零/复查使用 `mark_not_working`，避免 producer 与 consumer 竞争时丢失唤醒。
- callback 不持有 `EventConnection` 强引用，Session close 不形成 ownership cycle。
- Session 的应用侧方法当前要求串行调用；epoll callback 与应用线程之间的消息/关闭状态由 mutex + condition variable 保护。
- 当前不实现 fallback、queue-full retry、多 Stream、C++ server Session 或通用公共 API。

## Evidence

- 上游发送与唤醒：`third_party/shmipc-go/stream.go:199-251`、`session.go:616-634`。
- 上游接收：`third_party/shmipc-go/protocol_manager.go:258-287`、`session.go:560-607`。
- `tests/v2_client_session_test.cpp` 在 Linux 验证 20,000→17,000 字节跨 slice round-trip、超时和双向 close。
- 固定 Go oracle 以真实 Go server `AcceptStream/Read/Write/Close` 验证 C++ client；远端 Debug/ASan 与 50/50 重复通过。

## Links

- [父目录](../dirs/src__core.md)
- [v2 handshake](src__core__v2_handshake.hpp.md)
- [epoll dispatcher](src__transport__epoll_dispatcher.hpp.md)
- [Buffer IO](src__shm__buffer_io.hpp.md)
