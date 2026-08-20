# ADR-0002：异步 Stream callback 使用共享执行器与每流串行 pump

- 状态：Accepted
- 日期：2026-08-20
- 对应切片：`S-0502`

## 背景

固定 Go 实现通过 goroutine pool 执行 `OnData`、`OnLocalClose` 和
`OnRemoteClose`，并保证同一 Stream 的 callback 串行。C++ 版本还必须明确
RAII 注销、异常边界以及 callback 内关闭的等待规则，避免阻塞 event-loop、
每个 Stream 独占一个等待线程或析构自等待。

## 决策

1. 核心多路 Stream 只提供带 token 的非阻塞“状态可读”通知；通知复制后在锁外
   调用，且不直接执行用户 callback。
2. 公共 `CallbackExecutor` 是可由多个 Stream 共享的固定大小线程池。
3. 每个订阅维护一个串行 pump。通知只递增 generation，并在尚未调度时提交一个
   task；pump 用零超时 receive 排空消息。这样不同 Stream 可并行，同一 Stream
   永远不会并发回调，也不会丢失调度与退出之间到达的通知。
4. `CallbackSubscription` 独占注册关系。`stop()` 注销但不关闭 Stream；普通线程
   调用时等待正在执行的 callback，executor callback 内调用时不自等待。
5. 普通线程调用 `Stream::close()` 时，先关闭核心 Stream，再等待该 Stream 的
   callback 完成及 `on_local_close`；callback 内调用 `close()` 时只提出关闭并返回，
   pump 在 callback 返回后发送一次本地关闭通知并结束。
6. `on_data` 抛出的异常不得穿出库边界：先调用 `on_error(callback_error)`，再关闭
   Stream 并调用一次 `on_local_close`。其他 callback 抛出的异常被隔离。
7. callback 与 executor 由订阅共享持有；订阅同时保留底层 Stream 状态。因此移动或
   销毁外部 Stream handle 不会让正在执行的 callback 访问悬空对象。

## 公共契约

- 一个 Stream 同时只允许一个有效订阅，重复注册返回
  `callback_already_set`。
- `on_data`、终止 callback 和 `on_error` 对同一 Stream 串行。
- 远端关闭前已排队的消息先交付，然后调用一次 `on_remote_close`。
- 本地/远端终止 callback 至多一次；终止后订阅不再有效。
- callback 参数中的 `Stream&` 只在本次 callback 期间作为 handle 使用；需要发送或
  关闭时可直接调用，但不得保存其引用。
- `CallbackExecutor(0)` 抛出 `std::invalid_argument`。

## 结果与取舍

该结构避免“一 Stream 一阻塞线程”，也为后续 `S-0503` Listener 复用执行器保留
统一模型。代价是异步层需要 generation/scheduled 状态机和共享状态所有权；这些
复杂度由 callback 生命周期集成测试和 TSan 门禁约束。
