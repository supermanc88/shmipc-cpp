# 文件 `src/shm/queue_layout.hpp`

## Purpose

定义 queue 共享内存布局的内部类型和普通字节访问边界。它不导出安装，也不承诺原子并发语义。

## Constants（全量）

- `queue_header_size = 24`
- `queue_element_size = 12`
- `queue_count = 2`

## Enums（全量）

### `QueueArchitecture`

- `amd64`
- `arm64`

### `LayoutError`

- `none`
- `null_memory`
- `truncated_header`
- `zero_capacity`
- `size_overflow`
- `truncated_elements`
- `slot_out_of_range`
- `invalid_manager_alignment`

## Structs and Aliases（全量）

- `QueueOffsets`：`capacity`、`head`、`tail`、`working`、`elements` 五个 byte offsets。
- `QueueHeader`：`capacity uint32`、`head int64`、`tail int64`、`working uint32` 的值对象。
- `QueueElement`：`sequence_id`、`buffer_offset`、`status` 三个 uint32 的值对象。
- `LayoutResult<T>`：`value`、`error` 和显式成功布尔转换。
- `SizeResult = LayoutResult<size_t>`。
- `HeaderResult = LayoutResult<QueueHeader>`。
- `ElementResult = LayoutResult<QueueElement>`。

## Functions（全量）

- `to_string(QueueArchitecture)`：架构名称。
- `to_string(LayoutError)`：错误诊断文本。
- `queue_offsets(QueueArchitecture)`：选择 amd64/arm64 header offsets。
- `queue_region_size(uint32_t)`：checked 计算单 queue region 大小。
- `queue_manager_region_size(uint32_t, QueueArchitecture)`：checked 计算双 queue manager 大小并校验 arm64 对齐。
- `write_queue_header(uint8_t*, size_t, QueueArchitecture, const QueueHeader&)`：边界校验后写 header。
- `read_queue_header(const uint8_t*, size_t, QueueArchitecture)`：边界校验后读 header。
- `write_queue_element(uint8_t*, size_t, uint32_t, const QueueElement&)`：按 slot 写架构无关的 element。
- `read_queue_element(const uint8_t*, size_t, uint32_t)`：按 slot 读架构无关的 element。

## Edge Cases & Gotchas

- amd64 的 head/tail 在 offsets 4/12，不满足 C++ int64 对齐；实现只能通过安全的字节访问或后续明确验证的原子方案处理。
- element 布局不随架构变化，因此 element accessor 不接受目标架构参数。
- `memcpy` helper 只保证普通读写不触发未对齐/aliasing UB，不保证与并发 Go 对端的原子互操作。

## Evidence

- 声明：`src/shm/queue_layout.hpp:8-83`。
- 实现：`src/shm/queue_layout.cpp:49-192`。
- 测试：`tests/queue_layout_test.cpp:48-175`。
- Go oracle：`tools/go_oracle/control_header_oracle_test.gotxt:346-399`。

## Links

- [父目录](../dirs/src__shm.md)
- [架构决策](../02_DECISIONS.md)
