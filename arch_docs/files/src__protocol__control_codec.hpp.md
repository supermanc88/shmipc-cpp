# 文件 `src/protocol/control_codec.hpp`

## Purpose

定义内部 control-protocol codec 的稳定边界。它目前不安装到 `include/shmipc/`，因此不会提前承诺公共 ABI；后续握手和 transport 模块应依赖此接口，而不是重复解析线上字节。

## Constants

| 名称 | 值 | 含义 |
|---|---:|---|
| `header_size` | 8 | 控制头字节数 |
| `fallback_prefix_size` | 16 | header + stream ID + raw status |
| `magic` | `0x7758` | 控制协议 magic |
| `default_max_frame_length` | 64 MiB | 默认输入安全上限 |

## EventType

| 枚举 | 数值 |
|---|---:|
| `share_memory_by_file_path` | 0 |
| `polling` | 1 |
| `stream_close` | 2 |
| `fallback_data` | 3 |
| `exchange_protocol_version` | 4 |
| `share_memory_by_memfd` | 5 |
| `ack_share_memory` | 6 |
| `ack_ready_receive_fd` | 7 |
| `hot_restart` | 8 |
| `hot_restart_ack` | 9 |

## CodecError

| 枚举 | 触发条件 |
|---|---|
| `none` | 成功 |
| `truncated_header` | 输入为空或少于 8 字节 |
| `truncated_body` | 声明帧或 payload 字段不完整 |
| `invalid_length` | 长度小于 header 或超过上限 |
| `invalid_magic` | magic 不等于 `0x7758` |
| `invalid_version` | version 为 0 |
| `invalid_event_type` | event 不在 0..9 |
| `invalid_event_for_payload` | metadata/fallback decoder 收到错误事件 |
| `field_too_long` | metadata path 不能由 uint16 表示 |
| `trailing_bytes` | 输入包含声明帧之后的字节或未消费 body |

## Types

- `Header`：`length`、`version`、`type`。
- `SharedMemoryMetadata`：`header`、`queue_path`、`buffer_path`。
- `FallbackData`：`header`、`stream_id`、`raw_status`、`stream_state`、`payload`。
- `CodecResult<T>`：`value`、`error` 及成功布尔转换。
- `HeaderResult`：`CodecResult<Header>`。
- `BytesResult`：`CodecResult<std::vector<std::uint8_t>>`。
- `MetadataResult`：`CodecResult<SharedMemoryMetadata>`。
- `FallbackResult`：`CodecResult<FallbackData>`。

## Functions

- `to_string(EventType)`：返回与 Go 事件名一致的可读名称。
- `to_string(CodecError)`：返回稳定诊断文本。
- `encode_header(const Header&)`：生成 8 字节大端 header。
- `decode_header(const uint8_t*, size_t, uint32_t)`：解析并校验 header 字段和长度上限。
- `encode_shared_memory_metadata(uint8_t, EventType, const string&, const string&)`：生成 queue path 在前、buffer path 在后的 metadata frame。
- `decode_shared_memory_metadata(const uint8_t*, size_t, uint32_t)`：解析完整 metadata frame。
- `encode_fallback_data(uint8_t, uint32_t, uint32_t, const vector<uint8_t>&)`：生成 fallback frame。
- `decode_fallback_data(const uint8_t*, size_t, uint32_t)`：解析完整 fallback frame并保留 raw status。

## Evidence

- 声明：`src/protocol/control_codec.hpp:10-96`。
- 实现：`src/protocol/control_codec.cpp:58-275`。
- C++ 测试：`tests/control_header_golden_test.cpp`、`tests/protocol_codec_test.cpp:46-199`。
- Go oracle：`tools/go_oracle/control_header_oracle_test.gotxt:165-345`。

## Update Triggers

新增 event body、改变错误分类、帧上限、线上布局或把接口提升为公共 API 时，必须同步更新本页、golden、Go oracle、回归指南和 ADR。
