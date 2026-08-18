# 目录 `src/protocol/`

## Summary

实现 C++ 侧控制通道的生产编解码。当前 `S-0101` 覆盖 8 字节大端 header、事件类型 0..9、v2/v3 共享内存 metadata 及 fallback frame；不包含握手状态机、socket IO 或共享内存数据平面。

## Directory Contents

| 路径 | 类型 | 状态 | 说明 |
|---|---|---|---|
| `control_codec.hpp` | 内部头文件 | ✅ | 常量、事件/错误枚举、结果类型与 codec 接口 |
| `control_codec.cpp` | C++ 实现 | ✅ | 显式大端读写、完整帧校验及三类 payload 编解码 |

## Invariants

- 不把线上字节 reinterpret 为 C++ struct；所有多字节整数显式按网络大端访问。
- version 0、事件类型大于 9、长度小于 header、超过调用方上限或默认 64 MiB 的帧均被拒绝。
- payload decoder 只接受精确的一帧：声明长度不足返回 `truncated_body`，额外输入返回 `trailing_bytes`。
- metadata body 固定按 queue path、buffer path 顺序编码，各以前置 uint16 长度表示。
- fallback 保留完整 32 位 `raw_status`，并按 Go 接收语义暴露低 8 位 `stream_state`。

## Evidence

- `control_codec.cpp:11-54`：集中大端访问、事件分类和完整帧校验。
- `control_codec.cpp:110-275`：header、metadata 和 fallback 生产实现。
- `tests/control_header_golden_test.cpp`：事件 0..9 的 production header codec 与固定 fixture 一致。
- `tests/protocol_codec_test.cpp:46-199`：metadata/fallback round-trip 及截断、非法字段、错误事件、尾随字节、帧上限测试。
- `tools/go_oracle/control_header_oracle_test.gotxt:165-345`：同一 fixture 经固定 Go 真实编码路径验证。
- 2026-08-18：macOS AppleClang Debug/ASan+UBSan 与远端 Linux GCC 8.5 Debug/ASan 全部通过；提交 `603933e` 的 GitHub Actions run `32122127419` 七项作业全部成功。

## Links

- [完整接口索引](../files/src__protocol__control_codec.hpp.md)
- [Go 控制协议事实](../files/third_party__shmipc-go__protocol_event.go.md)
- [回归测试指南](../../docs/regression-test-guide.md)
