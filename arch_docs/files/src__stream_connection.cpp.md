# 文件 `src/stream_connection.cpp`

## Purpose

实现消息型 Stream 到连续字节流的 copy-based 适配，维护未读消息后缀和部分读取后的终止状态。

## Key Symbols

- `StreamConnection::Impl`：底层 Stream、reader mutex、pending buffer/offset 与 deferred status。
- `read`：先消费 pending；首次等待使用调用者 timeout，已有部分数据后只排空立即可读消息；终止错误在部分字节之后延迟到下一次 read。
- `write`：一次 send 对应一条消息，成功时 transferred 等于输入长度。

## Invariants

- read 不丢弃消息未读后缀，也不要求调用者感知消息边界。
- timeout 发生在已返回部分数据之后时，本次仍成功；非 timeout 终止状态延迟一次报告。
- adapter 析构关闭底层 Stream。

## Evidence

- 状态：`src/stream_connection.cpp:14-22`。
- read：`src/stream_connection.cpp:45-96`。
- write/deadline/close：`src/stream_connection.cpp:98-140`。

## Links

- [公共兼容层](include__shmipc__stream_connection.hpp.md)
- [公共 Session/Stream](include__shmipc__session.hpp.md)
