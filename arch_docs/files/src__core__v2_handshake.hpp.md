# 文件 `src/core/v2_handshake.hpp`

## Purpose

定义 v2 `/dev/shm` 文件路径握手的内部生产接口，并把成功映射的 buffer pool 与双向 queue 聚合为单一 move-only 生命周期对象。

## Types（全量）

- `V2HandshakeError`：`none`、`invalid_argument`、`transport_error`、`codec_error`、`unexpected_header`、`mapping_error`、`buffer_pool_error`、`queue_error`。
- `V2HandshakeStatus`：保留顶层分类、`errno` 及 transport/codec/mapping/pool/queue 子系统错误。
- `V2ClientConfig`：queue/buffer 路径、queue capacity、buffer region 大小及 tiers。
- `V2SharedMemory`：move-only 聚合 buffer/queue mappings、`BufferPool`、send queue、receive queue 与 creator 标志。
- `V2HandshakeResult`：`value + status`，支持显式成功判断。

## Functions（全量）

- `to_string(V2HandshakeError)`：稳定顶层错误文本。
- `v2_client_handshake(ControlSocket&, const V2ClientConfig&)`：创建 buffer、创建两个 queue、发送 v2 metadata，不等待 ACK。
- `v2_server_handshake(ControlSocket&, max_frame_length)`：exact-read header/body、校验 version/event/frame 上限，先映射 queue 再映射 buffer。

## Ownership & Error Model

- 调用不会消费 `ControlSocket`；成功后调用者负责把它转交后续事件层。
- client 返回值拥有创建文件的 unlink 责任；server 返回值只有 mapping ownership。
- 错误保留底层分类，调用者无需解析文本或只依赖 `errno`。
- 最大 metadata frame 为 `8 + 4 + 2*65535` 字节，与两个 uint16 路径长度字段的理论上界一致。

## Links

- [父目录](../dirs/src__core.md)
- [控制 codec](src__protocol__control_codec.hpp.md)
- [共享内存 mapping](src__shm__shared_memory_region.hpp.md)
- [control socket](src__transport__control_socket.hpp.md)
