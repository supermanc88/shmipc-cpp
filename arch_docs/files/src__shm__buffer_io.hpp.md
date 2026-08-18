# 文件 `src/shm/buffer_io.hpp`

## Purpose

在 `BufferPool` 的 allocation/chain 所有权之上提供内部连续读写抽象。Writer 负责按 Go 档位策略组成并发布 slice 链；Reader 负责单 slice 零拷贝、跨 slice 拷贝、pin/release 和最终回收。

## Enums（全量）

### `BufferIoError`

- `none`
- `null_data`
- `invalid_size`
- `no_buffer`
- `invalid_state`
- `out_of_range`
- `pool_error`

## Structs and Aliases（全量）

- `BufferIoResult<T>`：`value`、`BufferIoError` 与显式成功布尔转换。
- `MutableBufferView`：`reserve()` 返回的可写地址和长度。
- `MutableBufferViewResult`、`BufferReadViewResult`、`BufferReaderResult`：常用结果别名。
- `BufferReader::SliceCursor`（private）：单 slice token 及当前 read/write 边界。

## Classes（全量）

- `BufferReadView`：只读结果；单 slice 路径借用共享内存，跨 slice 路径持有连续 `std::vector` 副本。`data()`、`size()` 和 `is_zero_copy()` 明确区分两种生命周期。
- `BufferWriter`：不可复制、不可移动；提供 `write_byte`、`write_bytes`、`write_string`、`reserve`、`size`、`slice_count` 与 `publish`。未发布数据在析构时自动回收。
- `BufferReader`：不可复制、只可 move-construct；提供 `read_byte`、`read_bytes`、`peek`、`read_string`、`discard`、`remaining`、`pinned_slice_count` 与 `release_previous_read`。析构时回收未读和 pinned slices。

## Functions（全量）

- `to_string(BufferIoError)`：返回稳定诊断文本。
- `make_buffer_reader(pool, chain)`：复核每个已采用 slice header 及总数据长度，然后把 chain tokens 转移给 Reader。

## Read/Write Flow

1. Writer 对大于最大 slice 的剩余数据调用 `allocate_chain`，持续使用最大档位；较小尾部再选择最小可容纳档位，与 Go `allocShmBuffers` 一致。
2. `reserve` 只返回能够完全落在一个 slice 内的可写 view；`publish` 写 header 并把 root offset 交给 queue/对端。
3. Reader 从已 `adopt_chain` 的 tokens 创建 cursor。单 slice `read_bytes/peek` 返回借用 view 并 pin 当前 slice。
4. 跨 slice `read_bytes/peek` 复制为 owned view，不创建新的 pin；`read_byte`、`read_string` 与 `discard` 同样不创建新 pin。
5. 已耗尽且未 pinned 的 slice 会自动回收；`release_previous_read` 回收此前 pinned 的已读 slices，并允许当前已耗尽 slice 回收。

## Invariants & Gotchas

- `BufferPool` 及其底层 mapping 必须比 Writer、Reader 和所有零拷贝 `BufferReadView` 活得更久。
- 零拷贝 view 只在下一次覆盖相应共享内存之前有效；调用 `release_previous_read` 或销毁 Reader 后不得再访问。
- 跨 slice view 拥有副本，不受 Reader release/析构影响；这也是连续返回值的慢路径。
- `peek` 不推进 read cursor，但单 slice peek 仍会 pin；跨 slice peek 不 pin。
- 共享内存耗尽只返回 `no_buffer`；Stream 级 sticky fallback 尚未进入本模块，留给后续 Stream 集成。
- Writer/Reader 析构回收是 C++ RAII 加固，不改变跨进程 header 格式。

## Evidence

- 完整接口：`src/shm/buffer_io.hpp:12-140`。
- Writer 档位分配、写入、reserve、publish 与回滚：`src/shm/buffer_io.cpp:42-198`。
- Reader 零拷贝、跨片复制、pin/release 与回收：`src/shm/buffer_io.cpp:200-430`。
- chain header/总长度复核：`src/shm/buffer_io.cpp:452-491`。
- 生命周期和错误测试：`tests/buffer_io_test.cpp:34-228`。
- Go↔C++ 20,000 字节 helper 已改为真实 Writer/Reader：`tests/buffer_pool_interop_helper.cpp:18-85`。
- 本机 AppleClang Debug/ASan+UBSan/TSan、固定 Go oracle，以及远端 GCC 8.5 Debug/ASan 均通过；提交 `c1c23f9` 的 GitHub Actions run `32134325132` 七项作业全部成功。

## Links

- [父目录](../dirs/src__shm.md)
- [buffer pool](src__shm__buffer_pool.hpp.md)
- [架构决策](../02_DECISIONS.md)
- [回归测试指南](../../docs/regression-test-guide.md)
