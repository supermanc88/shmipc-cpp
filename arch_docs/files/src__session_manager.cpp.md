# 文件 `src/session_manager.cpp`

## Purpose

实现公共 SessionManager、per-Session FIFO Stream pool、generation 隔离、批量 round-robin 和独立断线重建 worker。

## Key Symbols

- `ManagerPool`：一个 Session、空闲 Stream 队列、generation、容量和 active 状态的锁保护聚合。
- `session_config`：为每个 PID/Session/generation 派生独立共享内存名称。
- `SessionManager::Impl::initialize/start_workers/monitor`：同步初始连接并为每池启动健康检查/重连循环。
- `SessionManager::Impl::shutdown`：原子发布停止，唤醒并 join workers，再关闭所有池资源。
- `PooledStream::return_to_pool`：验证 callback、核心 Stream 状态、Session 健康、generation 与容量后 FIFO 回池，否则关闭。
- `SessionManager::get_stream`：按固定 Go 公式选池，丢弃不再可复用的 idle Stream，必要时打开新 Stream。

## Control Flow

1. 工厂验证 manager/client 模式并同步建立配置数量的 Session。
2. 初始化成功后，每个池启动一个 monitor；健康检查发现控制连接关闭时递增 generation，移出并关闭旧 Session/idle Streams。
3. worker 按重连间隔反复建立同一 endpoint 的新 Session，只在池仍 active 且 generation 匹配时安装。
4. `get_stream` 原子递增选择计数，在目标池锁内优先 FIFO idle；checkout 再次验证状态以覆盖空闲期间到达的远端关闭/数据。
5. lease 归还时先清除 deadline 并验证核心状态，再在池锁内核对 active、generation、Session 健康和容量。
6. `close` 与析构共用幂等 shutdown；固定池数组不清空，使并发 `get_stream` 的索引读取保持有效。

## Invariants

- 旧 generation 的 checked-out lease 永不进入新 Session 的池。
- fallback Stream 永不复用；有未读消息、活动 callback、任一关闭/失败状态的 Stream 永不复用。
- 空闲池为有界 FIFO；容量为 0 时仍允许建流，但所有 lease 返回时关闭。
- worker 不持有池锁执行 close 或等待重连；安装新 Session 前重新核对停止/generation。
- 热重启 epoch 和连接/握手 timeout 不在本切片实现范围。

## Evidence

- 状态结构与派生名称：`src/session_manager.cpp:20-89`。
- 初始化、监控、重连和 shutdown：`src/session_manager.cpp:91-210`。
- lease 安全回收：`src/session_manager.cpp:212-274`。
- 选路与 checkout 二次验证：`src/session_manager.cpp:300-342`。
- Linux 运行测试：`tests/public_session_manager_test.cpp`；Debug 专项连续 20 轮及 ASan 已通过。

## Links

- [公共头](include__shmipc__session_manager.hpp.md)
- [Session 公共实现](src__session.cpp.md)
- [共享 Stream PImpl](src__public__session_impl.hpp.md)
- [内部多路 Session](src__core__v2_multiplexed_session.hpp.md)
- [根目录](../dirs/root.md)
- [架构关系](../graphs/relations.md)
