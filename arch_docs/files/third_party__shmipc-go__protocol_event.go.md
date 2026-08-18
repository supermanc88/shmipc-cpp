# `third_party/shmipc-go/protocol_event.go`

## 基础信息

- 文件类型：Go
- 包：`shmipc`
- 最后分析：2026-08-18
- 特征：结构特征文件；事件常量全量列出。

## Purpose

定义控制连接的 8 字节消息头、事件编号、fallback 前缀以及基础合法性检查。

## Exports（全量）

无。该文件全部符号为包内协议实现，但对跨语言兼容至关重要。

## Internal Constants（全量）

- `typeShareMemoryByFilePath = 0`
- `typePolling = 1`
- `typeStreamClose = 2`
- `typeFallbackData = 3`
- `typeExchangeProtoVersion = 4`
- `typeShareMemoryByMemfd = 5`
- `typeAckShareMemory = 6`
- `typeAckReadyRecvFD = 7`
- `typeHotRestart = 8`
- `typeHotRestartAck = 9`
- `minEventType = 0`
- `maxEventType = 9`

## Internal Types（全量）

- `header []byte`：8 字节大端控制头视图。
- `fallbackDataEvent [16]byte`：`header + uint32 stream_id + uint32 status`。

## Internal Functions/Methods（全量）

- `init()`：为版本 0..3 预编码 Polling header。
- `header.Length()`：读取 `[0:4]` 大端长度。
- `header.Magic()`：读取 `[4:6]` 大端 magic。
- `header.Version()`：读取 byte 6。
- `header.MsgType()`：读取 byte 7。
- `header.String()`：格式化 header。
- `header.encode()`：写入大端 header。
- `fallbackDataEvent.encode()`：写 header、Stream ID 和状态。
- `eventType.String()`：事件名映射。
- `checkEventValid()`：检查 magic、非零版本和事件范围。

## Control Flow

接收方先等待至少 8 字节，读取 `Length/Magic/Version/Type`，再由 type 对应 handler 判断 body 是否完整。handler 返回 consumed 和 stop，以支持一次 epoll read 中解析多个事件及半包保留。

## Edge Cases & Gotchas

- `Length` 是包含 header 的总长度。
- fallback 状态只消费 `uint32 status` 的低 8 位。
- 基础校验只拒绝 version 0，没有在此处拒绝高于本端最大版本；握手负责版本选择。
- C++ 必须先验证 `Length >= 8`、上限和整数溢出，再分配 body。

## Links

- [协议常量](third_party__shmipc-go__const.go.md)
- [架构概要](../01_OVERVIEW.md)
