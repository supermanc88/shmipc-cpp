# 目录 `tools/go_oracle/`

## Summary

以不修改 submodule 的方式运行固定 Go 参考实现内部 oracle，当前锁定 8 字节 control-header 及事件类型 0..9。

## Directory Contents

| 路径 | 类型 | 状态 | 说明 |
|---|---|---|---|
| `run_control_header_oracle.go` | Go runner | ✅ | 校验固定 commit、创建临时 overlay、调用上游 Go test |
| `control_header_oracle_test.gotxt` | Go test source | ✅ | 以 overlay 注入 `package shmipc`，调用未导出的 `header.encode` 核对 golden |

## Evidence

- `run_control_header_oracle.go:13` 固定 commit `55c241eea321071278d1ee7f7c46292d23e50a5b`。
- `run_control_header_oracle.go:31-38` 在运行 oracle 前读取并严格比较 submodule HEAD。
- `run_control_header_oracle.go:40-68` 仅在系统临时目录创建 overlay，随后调用上游 `go test` 并清理。
- `control_header_oracle_test.gotxt:18-27` 全量映射事件类型 0..9；`:71-75` 调用上游 `header.encode` 并逐字节比较。
- 提交 `34ef510` 的 GitHub Actions run `32119710781` 在 Go 1.25.10 下完成 setup、configure、build 和 test，作业结论 success。

## Guesses & Uncertainties

- 当前只锁定 header primitive，不声明各事件 body、合法 length 或状态机已兼容；这些属于 M1/M4。
- 上游 commit 升级必须显式更新 runner 常量、golden 来源和 ADR，不能静默接受。

## Links

- [项目根目录](root.md)
- [Go protocol_event.go](../files/third_party__shmipc-go__protocol_event.go.md)
- [项目回归测试指南](../../docs/regression-test-guide.md)
