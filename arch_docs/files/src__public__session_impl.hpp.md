# 文件 `src/public/session_impl.hpp`

## Purpose

定义仅供库内部同步、异步、观测与 Listener 层共享的 Stream/Session PImpl、EventLoop owner 和 callback 等待窄接口，保持安装头隔离。

## Types（全量）

- `AsyncCallbackControl`：只暴露 `wait_until_finished()` 的内部生命周期接口。
- `Stream::Impl`：持有内部 `core::V2Stream`、callback 注册 mutex/weak control、原子 local-close/closed 状态及共享 Logger 配置。
- `EventLoop`：共享拥有 `EpollDispatcher`，析构时 stop/join。
- `Session::Impl`：共享持有 EventLoop，以 variant 保存 client/server 多路 Session，并持有 Session ID、Monitor/Logger 与可停止/join 的周期指标 worker。
- `is_callback_executor_thread()`：close/stop 自等待规避的内部线程上下文查询。

## Invariants

- callback control 使用 weak ownership，subscription/state 反向强持有 PImpl，不形成环。
- `local_close_requested` 区分本地与远端终止 callback；`closed` 控制公共 handle 有效性。
- 本文件位于 private include path，不安装、不进入 public ABI。
- Listener 与 accepted Session 的 EventLoop shared ownership 保证关闭监听不影响已有连接。
- 字段声明顺序保证 telemetry worker 在核心 Session/mapping 析构前被显式停止；最终指标读取仍处于资源有效期。

## Evidence

- 定义：`src/public/session_impl.hpp:14-75`。
- 同步消费者：`src/session.cpp:223-240`。
- 异步消费者：`src/callback.cpp:161-443`。

## Links

- [公共 API](include__shmipc__session.hpp.md)
- [同步实现](src__session.cpp.md)
- [异步实现](src__callback.cpp.md)
- [Listener 实现](src__listener.cpp.md)
