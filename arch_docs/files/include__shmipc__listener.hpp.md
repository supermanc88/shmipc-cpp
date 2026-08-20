# 文件 `include/shmipc/listener.hpp`

## Purpose

定义版本无关的 move-only 服务监听 API。accepted Session 与 Listener 共享 event loop，关闭 Listener 后已有 Session 仍可用。

## Exports（全量）

- `ListenerConfig`：共享内存模式、backlog、公开握手帧上限，以及 accepted Session 共用的 Monitor/Logger/上报周期配置。
- `Listener`：有效性、绑定端口、限时 `accept_session` 与幂等 `close`。
- `ListenerResult`：Listener 与稳定 `Status`。
- `listen_tcp`、`listen_unix`：TCP/Unix 工厂；TCP 不支持 memfd。

## Invariants

- accept timeout 仅覆盖等待控制连接，握手在 accept 后同步完成。
- Listener 关闭只停止新连接，不关闭已经 accepted 的 Session。
- 类型不可复制、可 noexcept 移动；公共头不依赖内部 v2/v3 类型。
- 每个 accepted Session 独立生成 ID 和指标生命周期；配置中的 Monitor/Logger 可共享且必须线程安全。

## Evidence

- 全量声明：`include/shmipc/listener.hpp:12-65`。
- 生命周期实现：`src/listener.cpp:58-198`。
- v2/v3 工厂：`src/listener.cpp:200-259`。

## Links

- [公共 Session](include__shmipc__session.hpp.md)
- [Listener 实现](src__listener.cpp.md)
- [架构决策](../../docs/adr/0003-listener-event-loop.md)
