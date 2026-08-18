# 文件 `src/shm/shared_queue.hpp`

## Purpose

定义与 Go queue wire layout 兼容的运行期 MPSC queue view：同一进程内多个 producer 由本地 mutex 串行化，单个对端 consumer 通过共享 head/tail 原子同步；同时提供 working flag 和批量 pop。

## Enums（全量）

### `QueueError`

- `none`
- `null_memory`
- `truncated_region`
- `invalid_capacity`
- `invalid_architecture`
- `misaligned_atomic`
- `invalid_state`
- `full`
- `empty`

## Classes and Aliases（全量）

- `QueueResult<T>`：值、错误和显式成功判断。
- `SharedQueue`：不可复制、可 move-construct 的 non-owning queue view；提供 capacity/size/full/empty、`put`、`pop`、`pop_batch` 和 working flag 状态转换。
- `SharedQueueResult`、`QueueElementResult`、`QueueBatchResult`：对应结果别名。

## Functions（全量）

- `native_queue_architecture()`：编译期选择 amd64 或 arm64 运行布局。
- `to_string(QueueError)`：错误诊断文本。
- `initialize_shared_queue(memory, size, capacity, architecture)`：清零并原子初始化本机布局。
- `map_shared_queue(memory, size, architecture)`：校验 region、架构、原子地址和 head/tail 状态后建立 view。

## Control Flow

1. `put` 以进程内 mutex 串行化 producers，检查 `tail-head < capacity`，写完 12 字节 element 后 seq_cst 增加 tail。
2. 单 consumer 先读 head/tail，读取已发布 slot，再 seq_cst 增加 head；`pop_batch` 重复至上限或 empty。
3. producer 通过 `mark_working` 的 `0→1` CAS 判断是否需要发送 Polling；consumer 清零后重新检查 queue，若竞争期间出现数据则恢复为 1 并继续消费。

## Invariants & Gotchas

- 运行期只接受本机架构，foreign layout 仍可由 `queue_layout` 的普通 byte helpers 做离线验证。
- 32/64 位共享原子必须 always lock-free。arm64 head/tail 必须自然对齐；amd64 保留 Go 的 `+4/+12` 非自然对齐布局并由目标平台压力测试验证。
- mutex 只协调同一 `SharedQueue` 对象的本地 producers；协议假设每个方向只有一个进程生产、对端单 consumer，不支持多个进程同时生产同一方向。
- head/tail 必须非负、`head <= tail` 且距离不超过 capacity；计数单调增长并以 modulo 定位 slot。

## Evidence

- 完整接口：`src/shm/shared_queue.hpp:11-93`。
- 原子 put/pop、batch 和 working 状态机：`src/shm/shared_queue.cpp:74-173`。
- 初始化、映射和损坏状态校验：`src/shm/shared_queue.cpp:201-266`。
- MPSC、父子进程环绕及 1,000 轮唤醒竞争：`tests/shared_queue_test.cpp:20-253`。
- 双向 Go↔C++ queue manager：`tests/shared_queue_interop_helper.cpp:14-64`、`tools/go_oracle/control_header_oracle_test.gotxt:116-163`。

## Links

- [queue 布局](src__shm__queue_layout.hpp.md)
- [共享原子 primitive](src__shm__atomic_word.hpp.md)
- [父目录](../dirs/src__shm.md)
- [架构决策](../02_DECISIONS.md)
- [回归测试指南](../../docs/regression-test-guide.md)
