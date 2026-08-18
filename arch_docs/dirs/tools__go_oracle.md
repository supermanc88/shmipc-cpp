# 目录 `tools/go_oracle/`

## Summary

以不修改 submodule 的方式运行固定 Go 参考实现内部 oracle，当前锁定控制协议、amd64/arm64 queue、buffer manager/list/slice 布局及角色 counter 行为。

## Directory Contents

| 路径 | 类型 | 状态 | 说明 |
|---|---|---|---|
| `run_control_header_oracle.go` | Go runner | ✅ | 校验固定 commit、创建临时 overlay、调用上游 Go test |
| `control_header_oracle_test.gotxt` | Go test source | ✅ | 以 overlay 注入 `package shmipc`，调用真实 header、metadata 与 fallback 编码路径 |

## Evidence

- `run_control_header_oracle.go:13` 固定 commit `55c241eea321071278d1ee7f7c46292d23e50a5b`。
- `run_control_header_oracle.go:31-38` 在运行 oracle 前读取并严格比较 submodule HEAD。
- `run_control_header_oracle.go:40-71` 仅在系统临时目录创建 overlay，随后调用上游 `go test` 并清理。
- `control_header_oracle_test.gotxt:18-27` 全量映射事件类型 0..9；`:71-75` 调用上游 `header.encode` 并逐字节比较。
- `control_header_oracle_test.gotxt:87-142` 通过 `Session.generateShmMetadata` 验证 v2/v3；`:144-192` 通过 `fallbackDataEvent.encode` 验证 fallback。
- `control_header_oracle_test.gotxt:196-248` 通过 `createQueueFromBytes` 的真实指针映射验证当前 `runtime.GOARCH` 对应 queue golden；Darwin arm64 与 amd64 运行路径均通过。
- `control_header_oracle_test.gotxt:250-344` 直接计算 buffer 指针偏移，并以同一内存的 creator/mapper 两视图验证 `+20/+24` counter 独立增减。
- 提交 `34ef510` 的 GitHub Actions run `32119710781` 在 Go 1.25.10 下完成 setup、configure、build 和 test，作业结论 success。

## Guesses & Uncertainties

- 当前锁定控制协议正常编码、queue/buffer byte layout 与角色 counter 行为；握手状态机和跨语言原子语义仍属于后续切片，Go oracle 也不替代 C++ 异常输入测试。
- 上游 commit 升级必须显式更新 runner 常量、golden 来源和 ADR，不能静默接受。

## Links

- [项目根目录](root.md)
- [Go protocol_event.go](../files/third_party__shmipc-go__protocol_event.go.md)
- [项目回归测试指南](../../docs/regression-test-guide.md)
