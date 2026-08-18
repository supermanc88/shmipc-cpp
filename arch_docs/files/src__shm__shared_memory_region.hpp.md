# 文件 `src/shm/shared_memory_region.hpp`

## Purpose

定义内部 move-only 共享内存映射所有者。它统一管理 `munmap`、memfd descriptor 和创建端文件路径，供后续 buffer manager 与 queue manager 复用；当前不随 install 导出。

## Enums（全量）

### `SharedMemoryKind`

- `none`
- `file`
- `memfd`

### `FileCleanup`

- `keep`
- `unlink_on_destroy`

### `FdOwnership`

- `borrowed`：复制传入 descriptor，调用者继续拥有原 descriptor。
- `transferred`：从函数入口起接管 descriptor，成功或失败都由被调用方关闭。

### `MappingError`

- `none`
- `invalid_argument`
- `unsupported`
- `open_failed`
- `duplicate_failed`
- `stat_failed`
- `resize_failed`
- `map_failed`

## Structs, Classes and Aliases（全量）

- `MappingStatus`：`error`、原始 `system_error`（errno）和显式成功布尔转换。
- `MappingResult<T>`：`value`、`status` 和显式成功布尔转换。
- `SharedMemoryResult = MappingResult<SharedMemoryRegion>`。
- `SharedMemoryRegion`：不可复制、可移动的 mapping owner；暴露 `data/size/fd/kind/path` 观察接口及幂等 `reset()`。

## Functions（全量）

- `to_string(SharedMemoryKind)`：映射类型诊断文本。
- `to_string(MappingError)`：错误分类诊断文本。
- `create_file_region(path, size, cleanup)`：以 `O_EXCL` 创建、resize 并 `MAP_SHARED` 映射文件；默认创建者销毁时 unlink。
- `map_file_region(path)`：按 `fstat` 的真实正长度映射已有文件，mapper 不删除路径。
- `create_memfd_region(name, size)`：Linux 使用 `memfd_create`、`MFD_CLOEXEC`、`ftruncate` 和 `MAP_SHARED`；非 Linux 返回 `unsupported`。
- `map_memfd_region(fd, ownership)`：按明确借用/转移语义持有 descriptor，并映射 `fstat` 得到的完整区域。

## Ownership and Cleanup

- file descriptor 在文件 `mmap` 成功后立即关闭；mapping 仍有效。创建失败产生的半成品文件会删除。
- memfd descriptor 与 mapping 同寿命，`reset`/析构依次解除映射并关闭 descriptor。
- 只有创建端按默认策略 unlink 文件；mapper 只解除自己的 mapping，避免先退出的 mapper 删除仍用于握手的路径。
- move 后源对象为空，确保 mapping、descriptor 和路径清理各执行一次。

## Edge Cases & Gotchas

- size 必须大于零并可由 `off_t` 表示；已有文件也必须报告正长度。
- `borrowed` 会复制 descriptor，因此失败不改变调用者的 descriptor；`transferred` 即使映射失败也会关闭原 descriptor。
- `system_error` 只在底层系统调用失败时记录 errno；纯参数错误为 0。
- 该层只管理映射资源，不解释或验证映射内的 queue/buffer 字节布局。

## Evidence

- 声明：`src/shm/shared_memory_region.hpp:9-110`。
- RAII 与系统调用：`src/shm/shared_memory_region.cpp:20-315`。
- 文件双视图、move/unlink 与 memfd FD 所有权测试：`tests/shared_memory_region_test.cpp:21-137`。
- 远端 Kylin Linux/GCC 8.5 Debug 与 ASan：6/6 CTest 通过。

## Links

- [父目录](../dirs/src__shm.md)
- [架构决策](../02_DECISIONS.md)
- [回归测试指南](../../docs/regression-test-guide.md)
