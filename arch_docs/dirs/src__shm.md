# 目录 `src/shm/`

## Summary

承载共享内存布局、映射与数据平面实现。当前 `S-0102` 只实现 queue header/element 的 amd64 与 arm64 显式字节访问器；尚未实现 mmap/memfd RAII、原子 queue 算法或 buffer allocator。

## Directory Contents

| 路径 | 类型 | 状态 | 说明 |
|---|---|---|---|
| `queue_layout.hpp` | 内部头文件 | ✅ | 双架构偏移、布局类型、错误和访问接口 |
| `queue_layout.cpp` | C++ 实现 | ✅ | native-endian `memcpy` 访问、region size 与边界校验 |

## Layout

| 字段 | amd64 offset | arm64 offset | 大小 |
|---|---:|---:|---:|
| capacity | 0 | 0 | 4 |
| head | 4 | 8 | 8 |
| tail | 12 | 16 | 8 |
| working | 20 | 4 | 4 |
| elements start | 24 | 24 | — |

每个 element 固定 12 字节：`sequence_id`、`buffer_offset`、`status` 各占 native-endian uint32。一个 queue region 为 `24 + capacity * 12`，manager 连续放置两个 queue region。

## Invariants

- 不把 mmap 字节 reinterpret 为 C++ struct，也不对未对齐的 amd64 int64 字段做普通指针解引用。
- 所有访问前验证非空指针、最小 header、非零 capacity、计算后的 region size 和 slot 范围。
- arm64 queue manager 总映射长度必须为 16 的倍数，与 Go 映射检查一致。
- 当前 helper 是普通 byte accessor，不提供跨进程原子性；原子访问和内存序属于 `S-0204`。

## Evidence

- Go 事实：`third_party/shmipc-go/queue.go:175-209`。
- C++ 偏移和 region size：`src/shm/queue_layout.cpp:81-114`。
- C++ header/element 访问：`src/shm/queue_layout.cpp:116-192`。
- 双布局与错误测试：`tests/queue_layout_test.cpp:48-175`。
- Go 原生布局 oracle：`tools/go_oracle/control_header_oracle_test.gotxt:196-248`；Darwin arm64 与 amd64 运行路径均通过。
- 远端 Linux GCC 8.5 Debug/ASan：4/4 CTest 通过。

## Guesses & Uncertainties

- C++ 对外部 mmap 字段进行符合标准且与 Go 互操作的原子访问策略尚未决定；不得从当前 `memcpy` helper 推导并发安全。
- buffer list counter 的 `+20/+24` 差异仍由 `S-0103` 专项验证。

## Links

- [架构概要](../01_OVERVIEW.md)
- [决策与风险](../02_DECISIONS.md)
- [Go 参考实现目录](third_party__shmipc-go.md)
- [回归测试指南](../../docs/regression-test-guide.md)
