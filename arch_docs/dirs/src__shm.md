# 目录 `src/shm/`

## Summary

承载共享内存布局、映射与数据平面实现。当前 `S-0102..0104` 已实现 queue/buffer 显式布局与损坏链验证，`S-0201` 已实现 RAII mapping，`S-0202` 已实现单进程分级 buffer pool；跨进程原子 allocator 和 queue 算法尚未实现。

## Directory Contents

| 路径 | 类型 | 状态 | 说明 |
|---|---|---|---|
| `queue_layout.hpp` | 内部头文件 | ✅ | 双架构偏移、布局类型、错误和访问接口 |
| `queue_layout.cpp` | C++ 实现 | ✅ | native-endian `memcpy` 访问、region size 与边界校验 |
| `buffer_layout.hpp` | 内部头文件 | ✅ | manager/list/slice 类型、角色 counter 和访问接口 |
| `buffer_layout.cpp` | C++ 实现 | ✅ | buffer native-endian 访问与 checked region size |
| `buffer_pool.hpp` | 内部头文件 | ✅ | 分级 pool、move-only allocation token 与错误接口 |
| `buffer_pool.cpp` | C++ 实现 | ✅ | 单进程初始化/映射、分配回退、回收和完整性检查 |
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
- creator outstanding counter 位于 `+20`，mapper outstanding counter 位于 `+24`；`+28..35` 保留。
- slice header：20 字节，capacity/size/data-start/next 位于 `+0/+4/+8/+12`；flags 实际使用 `+16` 的低字节。

## Invariants

- 不把 mmap 字节 reinterpret 为 C++ struct，也不对未对齐的 amd64 int64 字段做普通指针解引用。
- 所有访问前验证非空指针、最小 header、非零 capacity、计算后的 region size 和 slot 范围。
- arm64 queue manager 总映射长度必须为 16 的倍数，与 Go 映射检查一致。
- 当前 helper 是普通 byte accessor，不提供跨进程原子性；原子访问和内存序属于 `S-0204`。
- buffer slice 必须满足 `data_start <= capacity` 且 `size <= capacity - data_start`。
- 静态链 validator 最多访问 `capacity` 个节点；offset 必须落在 region 内并按 slice stride 对齐，终止节点必须等于 tail。
- file mapping 创建端默认拥有 unlink 责任，mapper 仅 munmap；文件 FD 在 mmap 后关闭，memfd FD 则由 region 保留至销毁。
- borrowed memfd 会先复制 FD，transferred memfd 从调用入口起接管 FD；两种路径都设置/保留 close-on-exec 语义。
- 每个 buffer list 保留一个 sentinel，分配按最小合适档位开始并在耗尽后尝试更大档位；回收 token 必须匹配 memory、list 和 creator/mapper 角色。
- 当前 pool 的共享 header 更新不是原子操作，仅允许无并发修改的单进程路径；跨进程原子 free-list 属于 `S-0203`。

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
- `src/shm/buffer_pool.cpp:169-305,347-512`：分配/回收、初始化与映射校验。
- `tests/buffer_pool_test.cpp:18-216`：档位选择、耗尽回退、角色 counter、ownership 和损坏 header；本机与远端 Debug/Sanitizer 全部通过。

## Guesses & Uncertainties

- C++ 对外部 mmap 字段进行符合标准且与 Go 互操作的原子访问策略尚未决定；不得从当前 `memcpy` helper 推导并发安全。
- buffer list counter 的 `+20/+24` 已验证为 creator/mapper 角色隔离字段，不再是不确定项。

## Links

- [架构概要](../01_OVERVIEW.md)
- [决策与风险](../02_DECISIONS.md)
- [Go 参考实现目录](third_party__shmipc-go.md)
- [回归测试指南](../../docs/regression-test-guide.md)
