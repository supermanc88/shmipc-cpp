# 文件 `include/shmipc/session.hpp`

## Purpose

定义可安装、版本无关的同步 client API。应用只依赖公共配置、稳定错误分类与 move-only RAII `Session/Stream`，不感知 v2/v3 handshake、dispatcher 或共享内存内部类型。

## Exports（全量）

- `Error`、`Status`、`to_string(Error)`：稳定的公共错误分类、可选系统错误码与文本。
- `SharedMemoryMode`：`file` 或 `memfd` 资源模式。
- `BufferTier`、`ClientConfig`：queue/pool/tier 配置；默认值与固定 Go 配置一致。
- `Stream`：move-only 消息句柄，提供 ID、fallback 状态、同步 send/receive、persistent deadline、close 与远端关闭等待。
- `MessageResult`、`StreamResult`：owned payload/句柄与状态结果。
- `Session`：move-only client owner，提供 open/healthy 状态、开流和关闭。
- `SessionResult`：连接结果。
- `connect_tcp`、`connect_unix`：control connection 工厂；memfd 仅允许 Unix socket。

## Ownership & Concurrency

- `Session` 独占一个内部多路 client Session 和一个 event-loop thread；析构与显式 close 均先关闭连接，再停止/join event loop。
- `Stream` 独立持有内部连接/state 句柄，但 Session close 会关闭底层连接并使后续操作返回 `closed`。
- 不允许复制，move constructor/assignment 为 `noexcept`；空句柄可安全 close。
- 不同 Stream 可并发；同一 Stream mutation 由内部已验证 mutex/condition-variable 协议串行化。
- `receive` 返回 owned `std::vector<uint8_t>`，本接口不暴露 borrowed view，因而没有 public pin/release 生命周期。

## Boundaries

- file 模式中 `queue_name/buffer_name` 是路径，可配 TCP 或 Unix control socket。
- memfd 模式中两个名称仅用于诊断，必须使用 `connect_unix` 传递 FD；`connect_tcp` 在连接前返回 `unsupported`。
- public `Error` 刻意归并内部 codec/queue/pool/handshake 细分错误，避免协议实现成为安装 ABI。
- 当前只提供 client 主动开流；server Listener/Accept 属于 `S-0503`，异步 callback 属于 `S-0502`。

## Evidence

- 定义：`include/shmipc/session.hpp:13-172`。
- PImpl 与错误映射：`src/session.cpp:13-369`。
- Linux v2/v3 公共端到端：`tests/public_session_test.cpp:45-185`。
- 安装边界：`tests/package_consumer/` 仅包含公共头并链接 `shmipc::shmipc`。
- 本机 Debug/ASan+UBSan/TSan、远端 GCC 8.5 Debug/ASan 各 19/19；macOS/Linux package consumer 通过。

## Links

- [公共实现](src__session.cpp.md)
- [内部多路 Session](src__core__v2_multiplexed_session.hpp.md)
- [根目录](../dirs/root.md)
- [架构决策](../02_DECISIONS.md)
