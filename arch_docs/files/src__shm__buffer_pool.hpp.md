# 文件 `src/shm/buffer_pool.hpp`

## Purpose

定义分级共享内存 buffer pool 的内部跨进程接口。它在既有 manager/list/slice 字节布局上完成初始化、映射、原子分配回收，以及链式 slice 的分配、发布、接收和回收。

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
- `misaligned_atomic`
- `no_buffer`
- `invalid_allocation`
- `allocation_not_in_use`
- `counter_overflow`

## Classes and Aliases（全量）

- `BufferAllocation`：不可复制、只可 move-construct 的显式所有权 token；暴露 `data()`、`capacity()`、共享内存绝对 `offset()` 和有效性检查。成功回收后 token 失效。
- `BufferChain`：一组 allocation tokens 与有效数据总长；提供有效性和 root offset。
- `PublishedBufferChain`：发布后可跨进程传递的 root offset、slice 数量和有效数据总长。
- `BufferPool`：不可复制、可移动的非 owning pool view；除单 slice `allocate/recycle` 外，提供 `allocate_chain/publish_chain/adopt_chain/recycle_chain`。
- `BufferPoolCreateResult = BufferPoolResult<BufferPool>`。
- `BufferAllocationResult = BufferPoolResult<BufferAllocation>`。
- `BufferChainResult = BufferPoolResult<BufferChain>`。
- `PublishedBufferChainResult = BufferPoolResult<PublishedBufferChain>`。

## Functions（全量）

- `to_string(BufferPoolError)`：错误分类诊断文本。
- `initialize_buffer_pool(memory, size, tiers, role)`：排序并校验配置，按上游百分比分配公式初始化 manager、lists 和 slice free chains。
- `map_buffer_pool(memory, size, role)`：从现有字节映射 pool，验证稳定的 manager/list 顺序与 region 边界，以及动态 head/tail 的范围和 stride 对齐；不判定瞬时 size，也不要求并发活动的 free-list 是全空快照。

## Allocation Flow

1. 按 capacity 升序寻找第一个可容纳请求的 list；该档位只剩 sentinel 时继续尝试更大档位。
2. 校验 head 的范围、stride 对齐、free 状态与 next offset。
3. 先以原子 size 预留，再用 seq_cst CAS 取得 head 所有权；对已被其他消费者取走的旧 head 重试。
4. 回收先清理独占 slice，再以 CAS 推进 tail、链接旧 tail，最后发布 size；对已被其他生产者推进的旧 tail 重试。

## Chain Flow

- `allocate_chain` 与 Go 一致从最大档位向下分配，直到总 capacity 覆盖请求。
- `publish_chain` 写每个 slice 的有效 size 和下一个绝对共享内存 offset，然后使发送端 tokens 失效。
- `adopt_chain` 有界遍历、验证 slot/capacity/in-use/data range/cycle，并为接收角色重建 tokens。
- 接收端 `recycle_chain` 会减少自己的角色净计数；等量双向传输后 creator/mapper 两个净计数分别归零。

## Invariants & Gotchas

- tier capacity 唯一、非零且为 4 的倍数，percent 各自为 `1..100` 且总和严格等于 100；所有 offsets 必须可由 uint32 表示。
- 与 Go 一致，每个 list 永远保留最后一个 sentinel；因此可分配数量为共享 `size - 1`。
- 本地 token 只能由同角色 view 回收；跨进程发布后由接收端 `adopt_chain` 创建接收角色 token。失败不会使 token 失效，成功回收才失效。
- token 析构不会自动回收，因为后续跨进程传递会转移逻辑所有权；调用方必须显式 `recycle`，并可用 `all_returned()` 做关闭门禁。
- size/head/tail/角色计数使用 always-lock-free 32 位 seq_cst 原子；slice 普通字段的可见性由“写完后发布 size”和“成功 CAS 后取得独占所有权”建立。
- `all_returned()` 复现 Go 的检查：free size 恢复 capacity 且本角色净 pop/push counter 为零；单向传输结束时物理 slice 已归还但角色净计数可能非零。
- v2 无 ACK，mapper 可能在 creator 已分配 slice 后才建立；因此映射期不能深度遍历并要求 free-list 节点数等于瞬时 size，具体节点/chain 在 allocate/adopt/recycle 时验证。

## Evidence

- 完整接口：`src/shm/buffer_pool.hpp:11-156`。
- 原子分配、链式发布/adopt 与回收：`src/shm/buffer_pool.cpp:165-525`。
- 初始化与 live-pool 安全映射验证：`src/shm/buffer_pool.cpp:556-700`。
- 双进程压力、活动 allocation 映射和双向链测试：`tests/buffer_pool_test.cpp`。
- Go↔C++ helper：`tests/buffer_pool_interop_helper.cpp:17-75`；oracle：`tools/go_oracle/control_header_oracle_test.gotxt:18-113`。
- 本机 AppleClang Debug/ASan+UBSan/TSan、远端 GCC 8.5 Debug/ASan 和双向 Go oracle 通过。

## Links

- [父目录](../dirs/src__shm.md)
- [buffer 布局](src__shm__buffer_layout.hpp.md)
- [共享原子 primitive](src__shm__atomic_word.hpp.md)
- [架构决策](../02_DECISIONS.md)
- [回归测试指南](../../docs/regression-test-guide.md)
