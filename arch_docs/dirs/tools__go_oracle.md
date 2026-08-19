# 目录 `tools/go_oracle/`

## Summary

以不修改 submodule 的方式运行固定 Go 参考实现内部 oracle，锁定控制协议、queue/buffer 布局、角色 counter 行为，并驱动生产 BufferWriter/Reader、SharedQueue、v2/v3 handshake、单 Stream 与多 Stream Session helpers 验证互操作。

## Directory Contents

| 路径 | 类型 | 状态 | 说明 |
|---|---|---|---|
| `run_control_header_oracle.go` | Go runner | ✅ | 校验固定 commit、创建临时 overlay、调用上游 Go test |
| `control_header_oracle_test.gotxt` | Go test source | ✅ | 以 overlay 注入 `package shmipc`，调用真实控制协议、buffer pool 与 queue 路径 |

## Evidence

- `run_control_header_oracle.go:13` 固定 commit `55c241eea321071278d1ee7f7c46292d23e50a5b`。
- `run_control_header_oracle.go:31-38` 在运行 oracle 前读取并严格比较 submodule HEAD。
- `run_control_header_oracle.go:40-71` 仅在系统临时目录创建 overlay，随后调用上游 `go test` 并清理。
- `control_header_oracle_test.gotxt:165-238` 全量映射事件类型 0..9 并调用上游 `header.encode` 逐字节比较。
- `control_header_oracle_test.gotxt:239-345` 通过真实 metadata/fallback 编码路径验证 v2/v3 与 fallback。
- `control_header_oracle_test.gotxt:346-399` 通过 `createQueueFromBytes` 的真实指针映射验证当前 `runtime.GOARCH` 对应 queue golden；Darwin arm64 与 amd64 运行路径均通过。
- `control_header_oracle_test.gotxt:400-496` 直接计算 buffer 指针偏移，并以同一内存的 creator/mapper 两视图验证 `+20/+24` counter 独立增减。
- `control_header_oracle_test.gotxt:18-113` 调用 C++ helper：读取并回收 C++ 发布的 20,000 字节链，再发布 Go 链供 C++ 读取回收。
- `tests/buffer_pool_interop_helper.cpp:18-85` 是 oracle 的 C++ 创建/验证端；创建方向使用 BufferWriter，验证方向用 BufferReader 的跨片 owned-copy 路径，临时共享文件由 Go test 的 `t.TempDir()` 隔离清理。
- `control_header_oracle_test.gotxt:116-163` 与 `tests/shared_queue_interop_helper.cpp:14-64` 双向传递各 1,000 个 queue elements，并验证方向翻转和 working flag。
- `TestV2HandshakeInterop` 在 Linux 启动真实 Go `newSession` 与 C++ helper，分别验证 Go client→C++ server 和 C++ client→Go server；helper 以 mapping signal 保持 creator 生命周期，避免测试端过早断开。
- `TestProtocolVersionNegotiationInterop` 直接使用固定 Go `header.encode/checkEventValid`，双向验证 C++ client/server 的 8 字节 v3 offer/response；远端 Linux 连续 20 轮通过。
- `TestV3HandshakeInterop` 使用固定 Go `newSession` 和真实 memfd resources，双向验证版本协商、metadata、ready ACK、两个 FD、映射完成 ACK 及 queue/pool 参数；远端普通 100 轮、ASan helper 20 轮通过。
- `TestV2ClientSessionInterop` 在 Linux 以真实 Go server `AcceptStream/Read/Write/Close` 验证 C++ client 的 20,000→17,000 字节共享内存双向数据和关闭路径。
- `TestV2ServerSessionInterop` 在 Linux 以真实 Go client 首个 Stream ID 2 验证 C++ server 的三消息双向数据；独立子场景分别验证 C++ 与 Go 主动关闭。
- `TestV2MultiplexedSessionInterop` 在 Linux 双向验证 3 个连续 Stream：Go client→C++ server 与 C++ client→Go server 均检查 ID 2/3/4、跨 slice request/response 和主动关闭同步；远端普通 100 轮、ASan helper 20 轮通过。
- `TestV2FallbackInterop` 在 Linux 双向验证 1 KiB shared→2 MiB buffer-exhaustion fallback→257 B sticky fallback，并用反向 fallback ACK 将数据消费与关闭分隔；远端普通 50 轮、ASan helper 10 轮通过。
- `TestV3MultiplexedSessionInterop` 在 Linux 以真实 memfd Session 双向复验相同 shared→fallback→sticky→ACK→close 序列；远端普通 50 轮、ASan helper 10 轮通过。
- 提交 `0347f34` 的 GitHub Actions run `32158446306` 在 Linux 完整执行 Go protocol oracle，CTest 为 15/15。
- 提交 `78913e6` 的 GitHub Actions run `32204938990` 完整执行多 Stream oracle，CTest 为 16/16；Go client→C++ server 与 C++ client→Go server 的 ID 2/3/4 数据/关闭场景均通过。
- runner 支持 `SHMIPC_GO_ORACLE_COMPILE_LINUX_AMD64=<output>`，用于在本机生成包含 overlay 的静态 Linux/amd64 test binary，再同步到无 Go 工具链的远端执行。
- 提交 `34ef510` 的 GitHub Actions run `32119710781` 在 Go 1.25.10 下完成 setup、configure、build 和 test，作业结论 success。
- 提交 `c1c23f9` 的 run `32134325132` 中 Go protocol oracle 使用生产 BufferWriter/Reader 完成双向 20,000 字节链路，作业及其余六项矩阵全部成功。

## Guesses & Uncertainties

- 当前锁定控制协议正常编码、queue/buffer byte layout、buffer pool/queue 原子语义、Buffer IO 分档策略、v2/v3 握手及两个版本的多 Stream 数据面双向互操作；macOS 会明确跳过依赖 Linux Session 的子项，正式证据来自远端和 CI。Go oracle 不替代 C++ 异常输入测试。
- 上游 commit 升级必须显式更新 runner 常量、golden 来源和 ADR，不能静默接受。

## Links

- [项目根目录](root.md)
- [Go protocol_event.go](../files/third_party__shmipc-go__protocol_event.go.md)
- [项目回归测试指南](../../docs/regression-test-guide.md)
