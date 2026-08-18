# `third_party/shmipc-go/const.go`

## 基础信息

- 文件类型：Go
- 包：`shmipc`
- 最后分析：2026-08-18
- 特征：结构特征文件；以下常量和类型全量列出。

## Purpose

集中定义协议版本、共享内存模式、状态、默认容量和控制头尺寸，是 C++ wire/layout 常量的主要证据源。

## Exports（全量）

### Types

- `MemMapType uint8`：共享内存映射方式。

### Constants

- `MemMapTypeDevShmFile = 0`：使用 `/dev/shm` 文件。
- `MemMapTypeMemFd = 1`：使用 Linux memfd。

## Internal Types（全量）

- `eventType uint8`
- `sessionSateType uint32`

## Internal Constants（全量）

### 协议

- `protoVersion uint8 = 2`
- `maxSupportProtoVersion uint8 = 3`
- `magicNumber uint16 = 0x7758`

### Session 状态

- `defaultState = 0`
- `hotRestartState = 1`
- `hotRestartDoneState = 2`

### memfd、路径与时间

- `memfdCreateName = "shmipc"`
- `memfdDataLen = 4`
- `memfdCount = 2`
- `bufferPathSuffix = "_buffer"`
- `unixNetwork = "unix"`
- `hotRestartCheckTimeout = 2s`
- `hotRestartCheckInterval = 100ms`
- `sessionRebuildInterval = 60s`
- `epochIDLen = 8`
- `fileNameMaxLen = 255`
- `epochInfoMaxLen = 48`
- `queueInfoMaxLen = 27`

### 默认容量与固定布局

- `defaultQueueCap = 8192`
- `defaultShareMemoryCap = 32 MiB`
- `defaultSingleBufferSize = 4096`
- `queueElementLen = 12`
- `queueCount = 2`
- `sizeOfLength = 4`
- `sizeOfMagic = 2`
- `sizeOfVersion = 1`
- `sizeOfType = 1`
- `headerSize = 8`

## Variables（全量）

- `zeroTime time.Time`
- `pollingEventWithVersion [4]header`

## Edge Cases & Gotchas

- “默认协议版本 2”与“最大支持版本 3”同时存在；具体路径由映射模式和握手决定。
- `queueElementLen` 和 `headerSize` 是二进制兼容常量，C++ 必须 byte-level 验证。
- `epochInfoMaxLen` 与 `queueInfoMaxLen` 用于 Linux 文件名长度保护，不是 wire 字段。

## Links

- [控制事件格式](third_party__shmipc-go__protocol_event.go.md)
- [架构概要](../01_OVERVIEW.md)
