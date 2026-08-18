# 文件 `src/core/v2_multiplexed_session.hpp`

## Purpose

定义 v2 多路 Session 的内部 API，将连接级共享资源/事件分发与每个 Stream 的消息和关闭状态分离。固定 Go 兼容方向是 client `OpenStream`、server 在首个 opened element 到达时 `AcceptStream`。

## Types（全量）

- `V2Stream`：move-only Stream 句柄，提供 ID、消息级 `send/receive`、`close` 与远端关闭等待。
- `V2StreamResult`：Stream 创建/接受结果与细分 `V2SessionStatus`。
- `V2MultiplexedClientSession`：client 连接 owner，按 2、3、4… 连续分配 Stream ID。
- `V2MultiplexedServerSession`：server 连接 owner，按首个数据到达顺序接受未知非零 Stream ID。
- `V2MultiplexedClientSessionResult`、`V2MultiplexedServerSessionResult`：握手和 dispatcher 注册结果。
- `V2MultiplexedSessionState`：实现文件内定义的连接级 callback state，拥有共享内存、Stream 路由表、accept 队列与 Session failure。
- `V2StreamState`：实现文件内定义的 per-Stream 消息队列、条件变量和本地/远端关闭状态。

## Control Flow

1. client/server start 函数复用既有 v2 handshake，再将连接级 state 注册到 `EpollDispatcher`。
2. client `open_stream` 在发送前把新 ID 放入路由表；Open 本身不发送控制事件。
3. server callback drain receive queue；未知 opened ID 首次出现时创建 Stream、加入 accept 队列，然后路由消息。
4. 每个 queue element 的 buffer chain 被复制为一条 Stream 消息；不同 Stream 由各自 mutex/condition variable 隔离。
5. close 清理本地未读消息并只在 opened→local closed 时发送一次 closed element；若先收到 peer close，则本地 close 不发送 ACK，与固定 Go 状态机一致。
6. Session transport/codec/queue/pool failure 被扇出到所有现存 Stream 并唤醒 accept/receive/wait。

## Invariants & Boundaries

- 只承诺 client-originated Open→server Accept；不提供 server 主动开流，因为固定 Go client 不会为未知 ID 创建 Stream。
- client ID 严格从 2 开始连续递增；不实现上游注释所称但源码未遵守的奇偶分配。
- server 只有收到首条 opened 数据后才可 Accept；仅 Open 句柄不会产生 wire/queue 事件。
- `V2Stream` 强持有连接级 state 与 connection，避免 Session 外仍存句柄时悬空；资源清理测试必须先释放 Stream 句柄。
- control callback 不反向持有 connection，不形成 ownership cycle。
- 本切片仍为消息级 API；persistent deadline、queue-full retry/fallback、stream-map 回收和公共 API 属于后续 `S-0305b`/M4/M5。

## Evidence

- `tests/v2_multiplexed_session_test.cpp`：3 个 Stream（2/3/4）、并发首包、乱序 Accept、跨 slice 双向消息、两个主动关闭方向及资源生命周期；本地普通/ASan+UBSan/TSan 15/15，普通专项 100 次，远端 GCC 8.5 Debug/ASan 15/15 与专项 100 次通过。
- `TestV2MultiplexedSessionInterop`：真实 Go client→C++ server 和 C++ client→Go server 各 3 个 Stream；远端普通 100 轮与 ASan helper 20 轮通过。
- 调试证据：close 无 ACK 的错误测试预期及 helper 过早关闭 Session 的竞态均通过 runtime 日志/退出时序定位，临时插桩已清理。

## Links

- [父目录](../dirs/src__core.md)
- [单 client Session 基线](src__core__v2_client_session.hpp.md)
- [单 server Session 基线](src__core__v2_server_session.hpp.md)
- [架构决策](../02_DECISIONS.md)
