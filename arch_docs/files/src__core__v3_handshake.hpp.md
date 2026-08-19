# 文件 `src/core/v3_handshake.hpp`

## Purpose

组合 v3 版本协商、memfd metadata、Unix `SCM_RIGHTS`、共享内存映射、buffer pool 与双向 queue，形成可独立验收的完整 v3 资源初始化状态机。

## Public Surface

- `V3ClientConfig`：逻辑 queue/buffer 名、queue capacity、buffer region 大小与分档规格。
- `V3HandshakeError` / `V3HandshakeStatus`：保留版本协商、transport、codec、mapping、pool 与 queue 的嵌套错误来源。
- `V3SharedMemory`：move-only 资源聚合，持有两个 mapping、buffer pool、send/receive queue、逻辑名和 creator 角色。
- `v3_client_handshake`：创建两个 memfd，协商 v3，发送 metadata，等待 ready ACK，传递 FD，再等待 share-memory ACK。
- `v3_server_handshake`：协商 v3，读取 metadata，发送 ready ACK，接收并映射两个 FD，最后确认共享内存可用。

## Invariants

- client 发送的 FD 顺序固定为 `[buffer_fd, queue_fd]`；server 先映射 queue，再映射 buffer，但不改变线上的索引定义。
- server 必须恰好收到两个 FD；不足或超出都失败，所有未转移 descriptor 自动关闭。
- 物理 queue 前半段是 creator send、后半段是 creator receive；mapper 反向构造 receive/send 视图。
- 逻辑名仅用于 metadata 与诊断，不作为文件路径打开；资源本体完全由 memfd descriptor 提供。
- `AckReadyRecvFD` 必须先于 `SCM_RIGHTS`，`AckShareMemory` 只能在两个 mapping、pool 和 queue 均构造成功后发送。
- 握手借用 blocking `ControlSocket`；成功和失败都不转移控制 socket 所有权。
- 非 Linux 平台明确返回 unsupported，不伪造 v3 memfd 成功。

## Evidence

- 固定 Go 状态机：`third_party/shmipc-go/protocol_initializer.go`、`protocol_manager.go` 与 `block_io.go`。
- `tests/v3_handshake_test.cpp` 覆盖成功、queue 方向、错误 ACK、FD 数量、截断 metadata、参数错误和 descriptor 泄漏回归。
- `tests/v3_handshake_interop_helper.cpp` 与固定 Go `TestV3HandshakeInterop` 以真实 `newSession` 双向验证完整握手和实际 memfd resources。
- 本机 Debug、ASan+UBSan、TSan，远端 GCC 8.5 Debug/ASan，远端双向 Go oracle 普通 100 轮及 ASan helper 20 轮通过。

## Links

- [core 目录](../dirs/src__core.md)
- [control socket](src__transport__control_socket.hpp.md)
- [架构决策](../02_DECISIONS.md)
- [回归测试指南](../../docs/regression-test-guide.md)
