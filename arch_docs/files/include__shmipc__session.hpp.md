# 文件 `include/shmipc/session.hpp`

## Purpose

定义可安装、版本无关的 client/server Session API。应用只依赖公共配置、稳定错误分类、move-only RAII `Session/Stream` 与可选异步 callback 层，不感知 v2/v3 handshake、dispatcher 或共享内存内部类型。

## Exports（全量）

- `Error`、`Status`、`to_string(Error)`：稳定的公共错误分类、可选系统错误码与文本。
- `SharedMemoryMode`：`file` 或 `memfd` 资源模式。
- `BufferTier`、`ClientConfig`：queue/pool/tier 配置；默认值与固定 Go 配置一致。
- `Stream`：move-only 消息句柄，提供有效/打开状态、ID、fallback 状态、同步 send/receive、persistent deadline、close 与远端关闭等待。
- `MessageResult`、`StreamResult`：owned payload/句柄与状态结果。
- `StreamCallbacks`：数据、本地关闭、远端关闭和错误 callback 接口。
- `CallbackExecutor`：不可复制/移动、可跨 Stream 共享的固定线程池。
- `CallbackSubscription`、`CallbackSubscriptionResult`：move-only RAII callback 注册及结果。
- `Session`：move-only client 或 accepted-server owner，提供 open/healthy 状态、按角色 open/accept stream 和关闭。
- `SessionResult`：连接结果。
- `connect_tcp`、`connect_unix`：control connection 工厂；memfd 仅允许 Unix socket。

## Ownership & Concurrency

- `Session` 持有一个内部多路 client/server Session 和共享 EventLoop owner；最后一个 owner 释放时停止/join event loop。
- `Stream` 独立持有内部连接/state 句柄，但 Session close 会关闭底层连接并使后续操作返回 `closed`。
- 不允许复制，move constructor/assignment 为 `noexcept`；空句柄可安全 close。
- 不同 Stream 可并发；同一 Stream mutation 由内部已验证 mutex/condition-variable 协议串行化。
- 同一 Stream 的用户 callback 串行；不同 Stream 可在共享 executor 上并行。subscription 析构或普通线程 `stop()` 等待在途 callback，executor callback 内不自等待。
- callback/subscription 共享持有 Stream PImpl；callback 参数中的 `Stream&` 仅在本次调用期间有效，但可在 callback 内 send/close。
- `receive` 返回 owned `std::vector<uint8_t>`，本接口不暴露 borrowed view，因而没有 public pin/release 生命周期。

## Boundaries

- file 模式中 `queue_name/buffer_name` 是路径，可配 TCP 或 Unix control socket。
- memfd 模式中两个名称仅用于诊断，必须使用 `connect_unix` 传递 FD；`connect_tcp` 在连接前返回 `unsupported`。
- public `Error` 刻意归并内部 codec/queue/pool/handshake 细分错误，避免协议实现成为安装 ABI。
- client Session 只允许 `open_stream`，accepted server Session 只允许限时 `accept_stream`；错误角色返回 `unsupported`。
- 同一 Stream 只允许一个有效 subscription；`on_data` 异常被归一化为 `callback_error`，随后本地关闭。详细契约见 `docs/adr/0002-async-callback-executor.md`。

## Evidence

- 定义：`include/shmipc/session.hpp:15-199`。
- PImpl 与错误映射：`src/session.cpp:13-412`、`src/public/session_impl.hpp:44-75`。
- 异步执行与 subscription：`src/callback.cpp:56-443`；核心通知：`src/core/v2_multiplexed_session.cpp:270-344,675-740`。
- Linux v2/v3 公共端到端及异步生命周期：`tests/public_session_test.cpp`。
- 安装边界：`tests/package_consumer/` 仅包含公共头并链接 `shmipc::shmipc`。
- 本机 Debug/ASan+UBSan/TSan、远端 GCC 8.5 Debug/ASan 各 20/20；macOS/Linux package consumer 通过。

## Links

- [公共实现](src__session.cpp.md)
- [异步 callback 实现](src__callback.cpp.md)
- [共享 Stream PImpl](src__public__session_impl.hpp.md)
- [内部多路 Session](src__core__v2_multiplexed_session.hpp.md)
- [公共 Listener](include__shmipc__listener.hpp.md)
- [公共 SessionManager](include__shmipc__session_manager.hpp.md)
- [根目录](../dirs/root.md)
- [架构决策](../02_DECISIONS.md)
