# 目录 `src/shm/`

## Summary

承载共享内存布局、映射与数据平面实现。当前已具备 queue/buffer 显式布局、RAII mapping，以及可与 Go 双向传递链式 slice 的跨进程原子 buffer pool；并发 queue 算法尚未实现。

## Directory Contents

| 路径 | 类型 | 状态 | 说明 |
|---|---|---|---|
| `queue_layout.hpp` | 内部头文件 | ✅ | 双架构偏移、布局类型、错误和访问接口 |
| `queue_layout.cpp` | C++ 实现 | ✅ | native-endian `memcpy` 访问、region size 与边界校验 |
| `buffer_layout.hpp` | 内部头文件 | ✅ | manager/list/slice 类型、角色 counter 和访问接口 |
| `buffer_layout.cpp` | C++ 实现 | ✅ | buffer native-endian 访问与 checked region size |
| `buffer_pool.hpp` | 内部头文件 | ✅ | 分级 pool、move-only allocation token 与错误接口 |
| `buffer_pool.cpp` | C++ 实现 | ✅ | 原子分配回收、链式 publish/adopt 和完整性检查 |
| `atomic_word.hpp` | 内部头文件 | ✅ | always-lock-free 32 位 seq_cst 共享原子 primitive |
| `shared_memory_region.hpp` | 内部头文件 | ✅ | move-only mapping、错误模型及显式 FD/路径所有权 |
| `shared_memory_region.cpp` | C++ 实现 | ✅ | file/memfd 创建、映射和 RAII 清理 |

## Layout

| 字段 | amd64 offset | arm64 offset | 大小 |
|---|---:|---:|---:|
| capacity | 0 | 0 | 4 |
| head | 4 | 8 | 8 |
| tail | 12 | 16 | 8 |
| working | 20 | 4 | 4 |
| elements start | 24 | 24 | — |

每个 element 固定 12 字节：`sequence_id`、`buffer_offset`、`status` 各占 native-endian uint32。一个 queue region 为 `24 + capacity * 12`，manager 连续放置两个 queue region。

### Buffer layout

- manager header：8 字节，list count `+0`、used length `+4`。
- list header：36 字节，size/cap/head/tail/cap-per-buffer 位于 `+0/+4/+8/+12/+16`。
- creator 本地净 pop/push counter 位于 `+20`，mapper 对应字段位于 `+24`；字段允许为负，`+28..35` 保留。
- slice header：20 字节，capacity/size/data-start/next 位于 `+0/+4/+8/+12`；flags 实际使用 `+16` 的低字节。

## Invariants

- 不把 mmap 字节 reinterpret 为 C++ struct，也不对未对齐的 amd64 int64 字段做普通指针解引用。
- 所有访问前验证非空指针、最小 header、非零 capacity、计算后的 region size 和 slot 范围。
- arm64 queue manager 总映射长度必须为 16 的倍数，与 Go 映射检查一致。
- queue/buffer layout helper 是普通 byte accessor，不承诺原子性；buffer pool 已通过独立的 32 位 seq_cst primitive 提供跨进程原子访问，queue 的未对齐 64 位字段策略仍属于 `S-0204`。
- buffer slice 必须满足 `data_start <= capacity` 且 `size <= capacity - data_start`。
- 静态链 validator 最多访问 `capacity` 个节点；offset 必须落在 region 内并按 slice stride 对齐，终止节点必须等于 tail。
- file mapping 创建端默认拥有 unlink 责任，mapper 仅 munmap；文件 FD 在 mmap 后关闭，memfd FD 则由 region 保留至销毁。
- borrowed memfd 会先复制 FD，transferred memfd 从调用入口起接管 FD；两种路径都设置/保留 close-on-exec 语义。
- 每个 buffer list 保留一个 sentinel，分配按最小合适档位开始并在耗尽后尝试更大档位；回收 token 必须匹配 memory、list 和 creator/mapper 角色。
- pool 的 size/head/tail/counters 使用 lock-free seq_cst 32 位原子；tier capacity 与 list 起点必须保持 4 字节对齐。
- slice 普通字段先写完，再通过原子 size 发布；消费者只有成功 CAS head 后才取得 slice 独占权。
- chain next offset 是共享内存绝对 offset，与 free-list 内部使用的 list-relative offset 不同。

## Evidence

- Go 事实：`third_party/shmipc-go/queue.go:175-209`。
- C++ 偏移和 region size：`src/shm/queue_layout.cpp:81-114`。
- C++ header/element 访问：`src/shm/queue_layout.cpp:116-192`。
- 双布局与错误测试：`tests/queue_layout_test.cpp:48-175`。
- Go 原生布局 oracle：`tools/go_oracle/control_header_oracle_test.gotxt:196-248`；Darwin arm64 与 amd64 运行路径均通过。
- 远端 Linux GCC 8.5 Debug/ASan：4/4 CTest 通过。
- `src/shm/buffer_layout.cpp:82-225`：buffer checked size 与三类 header accessors。
- `tests/buffer_layout_test.cpp:50-177`：buffer golden 与错误路径。
- `tools/go_oracle/control_header_oracle_test.gotxt:250-344`：真实 creator/mapper pointer offsets 及独立 counter 行为。
- `src/shm/buffer_layout.cpp:199-240` 与 `tests/data/corpus/layout_corruption.txt`：有界链验证和 9 类损坏输入。
- `src/shm/shared_memory_region.cpp:100-315`：move-only 清理、file/memfd 系统调用及 FD 所有权。
- `tests/shared_memory_region_test.cpp:21-137`：文件双视图、创建端 unlink、move 和 Linux memfd 借用/转移测试；本机与远端 Debug/ASan 全部通过。
- `src/shm/atomic_word.hpp:9-38` 与 `src/shm/buffer_pool.cpp:36-525`：共享原子、竞争重试及 chain 生命周期。
- `tests/buffer_pool_test.cpp:261-410`：父子进程压力与双向 chain；本机 TSan/ASan+UBSan、远端 GCC 8.5 Debug/ASan 通过。
- `tests/buffer_pool_interop_helper.cpp:17-75` 与 Go oracle：C++→Go、Go→C++ 两方向 20,000 字节链通过。

## Guesses & Uncertainties

- queue 的 amd64 未对齐 64 位原子策略仍未决定；buffer pool 的 32 位字段策略已经验证。
- buffer list counter 偏移已确定，语义已修正为 creator/mapper 各自的本地净 pop/push 值。

## Links

- [架构概要](../01_OVERVIEW.md)
- [决策与风险](../02_DECISIONS.md)
- [Go 参考实现目录](third_party__shmipc-go.md)
- [回归测试指南](../../docs/regression-test-guide.md)
