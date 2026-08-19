# 文件 `src/core/v2_multiplexed_session.hpp`

## Purpose

定义 v2/v3 共用多路 Session 的内部 API，将连接级共享资源/事件分发与每个 Stream 的消息和关闭状态分离。固定 Go 兼容方向是 client `OpenStream`、server 在首个 opened element 到达时 `AcceptStream`。

## Types（全量）

- `V2Stream`：move-only Stream 句柄，提供 ID、消息级 `send/receive`、persistent read/write deadline、`close` 与远端关闭等待。
- `V2StreamResult`：Stream 创建/接受结果与细分 `V2SessionStatus`。
- `V2MultiplexedClientSession`：client 连接 owner，按 2、3、4… 连续分配 Stream ID。
- `V2MultiplexedServerSession`：server 连接 owner，按首个数据到达顺序接受未知非零 Stream ID。
- `V2MultiplexedClientSessionResult`、`V2MultiplexedServerSessionResult`：握手和 dispatcher 注册结果。
- `V3Stream`、`V3StreamResult`、`V3MultiplexedClientSession`、`V3MultiplexedServerSession`：复用同一数据面和生命周期实现的 v3 兼容别名。
- `V3MultiplexedClientSessionResult`、`V3MultiplexedServerSessionResult`：同时保留数据面状态与完整 v3 握手状态。
- `V2MultiplexedSessionState`：实现文件内定义的连接级 callback state，以 variant 拥有 v2/v3 共享资源、协议版本、Stream 路由表、accept 队列与 Session failure。
- `V2StreamState`：实现文件内定义的 per-Stream 消息队列、条件变量和本地/远端关闭状态。
- `SessionCircuitBreaker`：以 steady-clock atomic deadline 表示 Session 级 unhealthy 窗口，生产默认 30 秒。

## Control Flow

1. v2/v3 client/server start 函数分别复用对应 handshake，再把资源与协议版本写入共用 state 并注册到 `EpollDispatcher`。
2. client `open_stream` 在发送前把新 ID 放入路由表；Open 本身不发送控制事件。
3. server callback drain receive queue；未知 opened ID 首次出现时创建 Stream、加入 accept 队列，然后路由消息。
4. 每个 queue element 的 buffer chain 被复制为一条 Stream 消息；不同 Stream 由各自 mutex/condition variable 隔离。
5. close 清理本地未读消息并只在 opened→local closed 时发送一次 closed element；若先收到 peer close，则本地 close 不发送 ACK，与固定 Go 状态机一致。
6. Session transport/codec/queue/pool failure 被扇出到所有现存 Stream 并唤醒 accept/receive/wait。
7. send queue full 时按固定 Go 行为每 10ms 重试、最多 10 次；write deadline 只约束这段重试。close element 遇到 full 时改发控制通道 `StreamClose`。
8. `V2Stream::is_fallback()` 暴露当前 per-Stream sticky 状态；buffer 耗尽或消费对端 fallback payload 后变为 true，之后所有数据发送都走控制连接。
9. 发送或接收 FallbackData 打开 Session breaker；client `open_stream` 在窗口内返回 `unhealthy`，不消耗 Stream ID，已有 Stream 不受影响。

## Invariants & Boundaries

- 只承诺 client-originated Open→server Accept；不提供 server 主动开流，因为固定 Go client 不会为未知 ID 创建 Stream。
- client ID 严格从 2 开始连续递增；不实现上游注释所称但源码未遵守的奇偶分配。
- server 只有收到首条 opened 数据后才可 Accept；仅 Open 句柄不会产生 wire/queue 事件。
- `V2Stream` 强持有连接级 state 与 connection，避免 Session 外仍存句柄时悬空；资源清理测试必须先释放 Stream 句柄。
- control callback 不反向持有 connection，不形成 ownership cycle。
- 同一 Stream 的 send 以专用 mutex 保序；并发 close 先发布本地关闭状态并唤醒 retry，再等待在途 send 回收/退出，确保 close element 不越过数据。
- 本地 close 后保留状态以允许等待 Session failure/remote close，句柄释放时才从路由表移除；remote half-close 同样保留到应用本地 close/释放。Session failure 在扇出原始错误后清空路由与待 Accept 队列。
- 本层仍为消息级内部 API；v3 资源接入已完成。v2 类型保留兼容，v3 以别名复用实现，公共的版本中性 API 属于 M5。
- breaker 使用固定 Go 的 30 秒窗口；重复 fallback 不延长已打开窗口。短时长只用于独立状态机单测，不进入 Session 配置。

## Evidence

- `tests/v2_multiplexed_session_test.cpp`：3 个 Stream（2/3/4）、并发首包、乱序 Accept、跨 slice 双向消息、两个主动关闭方向、阻塞/持续 read deadline、write deadline、queue-full retry、close fallback、并发 send/close、Session failure 扇出及资源生命周期；本地普通/ASan+UBSan/TSan 15/15，普通专项 100 次，远端 GCC 8.5 Debug/ASan 15/15 与专项 100 次通过。
- `TestV2MultiplexedSessionInterop`：真实 Go client→C++ server 和 C++ client→Go server 各 3 个 Stream；远端普通 100 轮与 ASan helper 20 轮通过。
- `TestV2FallbackInterop`：两个方向均验证 shared→fallback→sticky 三消息顺序及反向 fallback ACK；远端普通 50 轮、ASan helper 10 轮通过。
- `tests/v3_multiplexed_session_test.cpp`：Linux 上执行完整 memfd Session 的 shared→fallback→sticky→ACK→close；非 Linux 明确验证 epoll unsupported。
- `TestV3MultiplexedSessionInterop`：真实固定 Go 与 C++ 两个方向完成同一 v3 数据序列；远端普通 50 轮、ASan helper 10 轮通过。
- `test_session_circuit_breaker` 验证 healthy→unhealthy、重复 open 不延长、到期恢复与再次打开；v2/v3 Session 和 Go oracle 验证两端拒绝新流及已有流继续。
- 调试证据：close 无 ACK 的错误测试预期及 helper 过早关闭 Session 的竞态均通过 runtime 日志/退出时序定位，临时插桩已清理。

## Links

- [父目录](../dirs/src__core.md)
- [单 client Session 基线](src__core__v2_client_session.hpp.md)
- [单 server Session 基线](src__core__v2_server_session.hpp.md)
- [架构决策](../02_DECISIONS.md)
