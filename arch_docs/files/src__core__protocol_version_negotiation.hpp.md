# 文件 `src/core/protocol_version_negotiation.hpp`

## Purpose

把固定 Go v3 初始化的第一阶段拆成独立、可双向验证的版本协商状态机；不创建共享内存，也不传递 FD。调用者依据 `negotiated_version` 决定后续进入 v2 文件路径或 v3 memfd 初始化。

## Public Internal Contract

- `minimum_protocol_version = 2`，`maximum_protocol_version = 3`。
- `negotiate_protocol_version_client` 发送 8 字节 `ExchangeProtoVersion(v3)`，读取服务端最高版本并选择两者较小值。
- `negotiate_protocol_version_server` 消费连接首帧；只有 version 3、type 4、length 8 合法，随后回复自身最高版本 3。
- `ProtocolVersionNegotiationResult` 同时保留 `peer_max_version` 与 `negotiated_version`，使 v2 降级成为显式结果。
- socket 由调用者持有；函数只借用 blocking `ControlSocket`，错误时不关闭连接。

## Compatibility Invariants

- 客户端收到服务端 version 2 是合法降级；收到大于 3 的未来版本仍选择 3。
- 服务端不能接受 version 2 的 Exchange 帧：固定 Go 服务端先按首帧 version 选择 initializer，version 2 会进入文件路径初始化并把 type 4 判为错误。
- 服务端响应宣告本地最高版本 3，而不是回显或发送已选择版本。
- 项目安全策略额外要求协商帧 `length == 8`；magic/version/type 先复用生产 codec 校验。

## Error Model

- `invalid_argument`：无效 socket。
- `transport_error`：exact read/write 的 EOF、would-block 或系统错误，并保留 transport/errno。
- `codec_error`：非法 magic、零版本或非法事件值。
- `unexpected_header`：错误事件、错误长度或服务端非 v3 首帧。
- `unsupported_version`：客户端收到低于 v2 的对端版本。

## Evidence

- 上游：`third_party/shmipc-go/protocol_manager.go:75-117,179-184`、`protocol_initializer.go:75-120`、`protocol_event.go:54-132`。
- C++：`src/core/protocol_version_negotiation.cpp` 与 `tests/protocol_version_negotiation_test.cpp`。
- 双向 oracle：`tests/protocol_version_negotiation_interop_helper.cpp`、`tools/go_oracle/control_header_oracle_test.gotxt`。
- 本机 Debug、ASan+UBSan、TSan 16/16；本机固定 Go oracle 17/17。
- 远端 Linux GCC 8.5 Debug/ASan 16/16；交叉编译固定 Go oracle 的两个方向连续 20 轮通过。

## Links

- [core 目录](../dirs/src__core.md)
- [架构决策](../02_DECISIONS.md)
- [回归测试指南](../../docs/regression-test-guide.md)
