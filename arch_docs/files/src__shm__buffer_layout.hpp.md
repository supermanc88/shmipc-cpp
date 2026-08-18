# 文件 `src/shm/buffer_layout.hpp`

## Purpose

定义 buffer manager、buffer list 与 buffer slice 的内部共享内存值类型、角色 counter 和普通字节访问接口。接口不安装，也不提供原子并发语义。

## Constants（全量）

- `buffer_manager_header_size = 8`
- `buffer_list_header_size = 36`
- `buffer_slice_header_size = 20`

## Enums（全量）

### `BufferListRole`

- `creator`：counter offset 20。
- `mapper`：counter offset 24。

### `BufferLayoutError`

- `none`
- `null_memory`
- `truncated_header`
- `invalid_field`
- `size_overflow`
- `truncated_region`

## Structs and Aliases（全量）

- `BufferManagerHeader`：`list_count uint16`、`used_length uint32`。
- `BufferListHeader`：`size int32`、`capacity/head/tail/capacity_per_buffer uint32`、`creator_counter/mapper_counter int32`。
- `BufferSliceHeader`：`capacity/size/data_start/next_offset uint32`、`flags uint8`。
- `BufferLayoutResult<T>`：`value`、`error` 和显式成功布尔转换。
- `BufferSizeResult = BufferLayoutResult<size_t>`。
- `BufferManagerHeaderResult = BufferLayoutResult<BufferManagerHeader>`。
- `BufferListHeaderResult = BufferLayoutResult<BufferListHeader>`。
- `BufferSliceHeaderResult = BufferLayoutResult<BufferSliceHeader>`。

## Functions（全量）

- `to_string(BufferListRole)`：角色名称。
- `to_string(BufferLayoutError)`：错误诊断文本。
- `buffer_list_counter_offset(BufferListRole)`：按角色返回 20 或 24。
- `buffer_list_region_size(uint32_t, uint32_t)`：checked 计算 `36 + capacity * (20 + capacity_per_buffer)`。
- `write_buffer_manager_header(...)` / `read_buffer_manager_header(...)`：manager header 访问与 used-region 校验。
- `write_buffer_list_header(...)` / `read_buffer_list_header(...)`：list header 访问、容量和 region 校验。
- `write_buffer_slice_header(...)` / `read_buffer_slice_header(...)`：slice header 访问并校验 data range。

## Invariants

- creator/mapper counters 是不同字段，不能合并。
- `size` 必须在 `[0, capacity]`。
- slice 必须满足 `data_start <= capacity` 且 `size <= capacity - data_start`。
- flags 只读写 offset 16 的低字节，offset 17..19 保留。
- 当前 `memcpy` accessors 不保证与并发 Go 原子操作互操作。

## Evidence

- 声明：`src/shm/buffer_layout.hpp:8-87`。
- 实现：`src/shm/buffer_layout.cpp:49-225`。
- C++ 测试：`tests/buffer_layout_test.cpp:50-177`。
- Go layout/行为 probe：`tools/go_oracle/control_header_oracle_test.gotxt:250-344`。
- 上游演进：commit `8ab38be` 将两个 uint64 push/pop counters 替换为角色相关 int32 counter。

## Links

- [父目录](../dirs/src__shm.md)
- [决策与风险](../02_DECISIONS.md)
