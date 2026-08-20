# 文件 `src/callback.cpp`

## Purpose

实现公共异步 Stream callback 适配层：共享固定线程池、每 Stream 串行 pump、RAII subscription、异常隔离和关闭等待。它消费核心的非阻塞 readable notifier，但所有用户 callback 只在 executor worker 上执行。

## Key Symbols

- `CallbackExecutor::Impl`：mutex/condition/deque 任务队列和固定 worker 集合；构造失败、普通析构及 worker 内最后释放均安全回收线程。
- `AsyncCallbackState`：每订阅 generation/scheduled/stopping/finished 状态机；强持有 callback、executor 与 Stream PImpl。
- `CallbackSubscription::stop`：注销而不关闭 Stream；普通线程等待，executor callback 内避免自等待。
- `Stream::set_callbacks`：参数/关闭/重复注册门禁，创建 subscription 并注册核心 notifier。

## Control Flow

1. 注册时在 Stream callback mutex 下发布 control，并取得核心 notifier token；随后主动通知一次以处理注册前已就绪状态。
2. notifier 只增加 generation；若该 Stream 尚无 task，则提交一个 pump 到共享 executor。
3. pump 以零超时 `receive` 排空消息并调用 `on_data`。退出前若 generation 已变化则继续，否则清除 scheduled。
4. 本地/远端关闭或错误形成终态 callback，然后 tokenized 注销并唤醒 stop/close 等待者。
5. `on_data` 抛异常时先隔离为 `callback_error`，再关闭 Stream 并发布一次本地终止 callback。

## Invariants

- 每个 AsyncCallbackState 最多有一个 scheduled pump，故同 Stream callback 永不并发；不同 state 可占用不同 worker。
- 核心 notifier 不强持有 AsyncCallbackState；task 和 subscription 强持有 state，避免悬空与 ownership cycle。
- `finish` 幂等，终止 callback 由独立 flag 保证至多一次。
- executor worker 上的 close/stop 不同步等待 executor callback；普通线程 close/stop 必须观察 finished。

## Evidence

- executor 与异常安全：`src/callback.cpp:56-155`。
- generation pump 与关闭处理：`src/callback.cpp:161-367`。
- subscription 与注册门禁：`src/callback.cpp:369-443`。
- `tests/public_session_test.cpp`：两 Stream 并行/每流串行、重复注册、远端关闭、外部 close 等待、callback 内 close、callback 异常与终止唯一性；专项连续 20 轮，本机三套 sanitizer、远端 GCC 8.5 Debug/ASan 通过。

## Links

- [公共 API](include__shmipc__session.hpp.md)
- [共享 Stream PImpl](src__public__session_impl.hpp.md)
- [同步/连接适配](src__session.cpp.md)
- [核心多路 Stream](src__core__v2_multiplexed_session.hpp.md)
- [架构决策](../02_DECISIONS.md)
