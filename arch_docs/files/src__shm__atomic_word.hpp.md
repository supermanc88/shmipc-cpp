# 文件 `src/shm/atomic_word.hpp`

## Purpose

提供共享 mmap 中 32/64 位整数的内部原子 primitive，供 buffer pool 与 queue 复用。实现固定使用 GCC/Clang `__atomic` 顺序一致操作，与 Go `sync/atomic` 的默认顺序一致。

## Exports（全量）

- `detail::atomic_word_aligned<T>(const T*)`：检查 32/64 位 word 的自然对齐。
- `detail::atomic_load<T>(const T*)`：seq_cst 原子读取。
- `detail::atomic_store<T>(T*, T)`：seq_cst 原子写入。
- `detail::atomic_fetch_add<T>(T*, T)`：seq_cst 原子加并返回旧值。
- `detail::atomic_compare_exchange<T>(T*, T&, T)`：seq_cst strong CAS；失败时更新 expected。

## Invariants & Gotchas

- 编译期要求 32 位与 64 位原子 always lock-free；跨进程不能依赖进程内 fallback lock。
- 模板只接受 4/8 字节 integral 类型。buffer 字段和 arm64 queue 字段要求自然对齐；amd64 queue 为兼容 Go 保留 `+4/+12` 的非自然对齐 64 位访问。
- 该文件依赖 GCC/Clang `__atomic` builtins，符合当前 AppleClang/GCC/Clang 工具链，不构成 MSVC 支持承诺。

## Evidence

- lock-free 约束与完整接口：`src/shm/atomic_word.hpp:9-53`。
- buffer pool 使用：`src/shm/buffer_pool.cpp:36-68,165-525`。
- queue 使用：`src/shm/shared_queue.cpp:74-173,201-266`。
- 验证：AppleClang Debug、ASan+UBSan、TSan，以及远端 GCC 8.5 Debug/ASan 通过。

## Links

- [buffer pool](src__shm__buffer_pool.hpp.md)
- [shared queue](src__shm__shared_queue.hpp.md)
- [父目录](../dirs/src__shm.md)
- [架构决策](../02_DECISIONS.md)
