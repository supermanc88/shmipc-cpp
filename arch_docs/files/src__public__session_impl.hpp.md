# 文件 `src/public/session_impl.hpp`

## Purpose

定义仅供库内部同步、异步与 Listener 层共享的 Stream/Session PImpl、EventLoop owner 和 callback 等待窄接口，保持安装头隔离。

## Types（全量）

- `AsyncCallbackControl`：只暴露 `wait_until_finished()` 的内部生命周期接口。
- `Stream::Impl`：持有内部 `core::V2Stream`、callback 注册 mutex/weak control，以及原子 local-close/closed 状态。
- `EventLoop`：共享拥有 `EpollDispatcher`，析构时 stop/join。
- `Session::Impl`：共享持有 EventLoop，并以 variant 保存 client 或 server 多路 Session。
- `is_callback_executor_thread()`：close/stop 自等待规避的内部线程上下文查询。

## Invariants

- callback control 使用 weak ownership，subscription/state 反向强持有 PImpl，不形成环。
- `local_close_requested` 区分本地与远端终止 callback；`closed` 控制公共 handle 有效性。
- 本文件位于 private include path，不安装、不进入 public ABI。
- Listener 与 accepted Session 的 EventLoop shared ownership 保证关闭监听不影响已有连接。

## Evidence

- 定义：`src/public/session_impl.hpp:14-75`。
- 同步消费者：`src/session.cpp:223-240`。
- 异步消费者：`src/callback.cpp:161-443`。

## Links

- [公共 API](include__shmipc__session.hpp.md)
- [同步实现](src__session.cpp.md)
- [异步实现](src__callback.cpp.md)
- [Listener 实现](src__listener.cpp.md)
