# 文件 `include/shmipc/stream_connection.hpp`

## Purpose

将消息型 `Stream` 包装为 copy-based 字节流接口，供偏好连续 Read/Write 语义的调用方使用。

## Exports（全量）

- `TransferResult`：本次传输字节数与稳定 `Status`。
- `StreamConnection`：move-only adapter；提供 `read`、`write`、三类 deadline、`id` 和 `close`。

## Invariants

- 一次 write 发布一条 shmipc 消息；read 隐藏消息边界并保存未读后缀。
- 一个 reader 与一个 writer 可并行；close 与同一对象上的其他操作由调用方同步。
- 这是兼容 copy 路径；需要共享内存所有权/零拷贝时使用原生 `Stream`。

## Evidence

- 公共契约：`include/shmipc/stream_connection.hpp:13-51`。
- 缓冲与传输实现：`src/stream_connection.cpp:14-140`。

## Links

- [公共 Session/Stream](include__shmipc__session.hpp.md)
- [适配实现](src__stream_connection.cpp.md)
