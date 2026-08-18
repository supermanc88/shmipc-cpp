# 文件 `src/shm/buffer_pool.hpp`

## Purpose

定义分级共享内存 buffer pool 的内部单进程接口。它在既有 manager/list/slice 字节布局上完成初始化、映射、最小合适档位分配、向大档位回退和显式回收；当前不提供并发或跨进程原子保证。

## Structs（全量）

- `BufferTierSpec`：单 slice `capacity` 与该档位占 buffer region 的 `percent`。
- `BufferPoolResult<T>`：`value`、`BufferPoolError` 和显式成功布尔转换。
- `BufferPool::ListView`（private）：单 list 的绝对 offset、region size 与 slice capacity。

## Enums（全量）

### `BufferPoolError`

- `none`
- `null_memory`
- `invalid_config`
- `size_overflow`
- `truncated_region`
- `invalid_layout`
- `no_buffer`
- `invalid_allocation`
- `allocation_not_in_use`
- `counter_overflow`

## Classes and Aliases（全量）

- `BufferAllocation`：不可复制、只可 move-construct 的显式所有权 token；暴露 `data()`、`capacity()`、共享内存绝对 `offset()` 和有效性检查。成功回收后 token 失效。
- `BufferPool`：不可复制、可移动的非 owning pool view；暴露 list 数量、最小/最大档位、已用映射长度、可分配字节数、全部归还检查、`allocate` 与 `recycle`。
- `BufferPoolCreateResult = BufferPoolResult<BufferPool>`。
- `BufferAllocationResult = BufferPoolResult<BufferAllocation>`。

## Functions（全量）

- `to_string(BufferPoolError)`：错误分类诊断文本。
- `initialize_buffer_pool(memory, size, tiers, role)`：排序并校验配置，按上游百分比分配公式初始化 manager、lists 和 slice free chains。
- `map_buffer_pool(memory, size, role)`：从现有字节映射 pool，并验证 list 顺序、region 边界、free chain、节点状态及 header size 一致性。

## Allocation Flow

1. 按 capacity 升序寻找第一个可容纳请求的 list；该档位只剩 sentinel 时继续尝试更大档位。
2. 校验 head 的范围、stride 对齐、free 状态与 next offset。
3. 从 free-chain 取出 head，清除 has-next、设置 in-use，并增加本角色 outstanding counter。
4. 回收时验证 token 的 memory/list/role、in-use 状态和 tail，再将 slice 接到 free-chain 尾部并减少本角色 counter。

## Invariants & Gotchas

- tier capacity 唯一且非零，percent 各自为 `1..100` 且总和严格等于 100；所有 offsets 必须可由 uint32 表示。
- 与 Go 一致，每个 list 永远保留最后一个 sentinel；因此可分配数量为共享 `size - 1`。
- creator token 不能由 mapper view 回收，反之亦然；失败不会使 token 失效，成功回收才失效。
- token 析构不会自动回收，因为后续跨进程传递会转移逻辑所有权；调用方必须显式 `recycle`，并可用 `all_returned()` 做关闭门禁。
- 当前读改写使用普通 byte accessors，只支持无并发修改的单进程验证；不得用于多线程或两个进程同时 allocate/recycle。原子算法属于 `S-0203`。

## Evidence

- 完整接口：`src/shm/buffer_pool.hpp:11-125`。
- 配置、分配与回收：`src/shm/buffer_pool.cpp:57-305`。
- 初始化与严格映射验证：`src/shm/buffer_pool.cpp:347-512`。
- 档位选择、耗尽回退、角色 counter、错误 ownership 与损坏 header 测试：`tests/buffer_pool_test.cpp:18-216`。
- 本机 AppleClang Debug/ASan+UBSan 与远端 GCC 8.5 Debug/ASan：7/7 CTest 通过。

## Links

- [父目录](../dirs/src__shm.md)
- [buffer 布局](src__shm__buffer_layout.hpp.md)
- [架构决策](../02_DECISIONS.md)
- [回归测试指南](../../docs/regression-test-guide.md)
