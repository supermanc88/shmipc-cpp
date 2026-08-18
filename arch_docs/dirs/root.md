# 项目根目录 `/`

## Summary

当前仓库处于迁移启动阶段：只有固定 Go 参考实现、架构记忆和移植计划，尚无 C++ 构建与源码骨架。

## Directory Contents（深度=1）

| 路径 | 类型 | 状态 | 说明 |
|---|---|---|---|
| `.gitmodules` | 文件 | ✅ | 声明 `third_party/shmipc-go` 子模块 |
| `arch_docs/` | 目录 | ✅ | 架构分析与证据索引 |
| `docs/` | 目录 | ✅ | C++ 移植计划与项目标准工作流 |
| `third_party/` | 目录 | ✅ | 上游参考实现聚合目录 |

## Evidence

- `.gitmodules` 将 Go 参考实现指向 `https://github.com/cloudwego/shmipc-go.git`。
- 当前 C++ 工程尚无 `CMakeLists.txt`、`include/`、`src/` 或 `tests/`。

## Guesses & Uncertainties

- C++ 标准、库类型、包命名空间、依赖策略和发布平台尚未由用户确认。
- 推荐目录和里程碑见 [移植计划](../../docs/SHMIPC_CPP_PORTING_PLAN.md)。
- 远程 Linux 同步、构建和测试见 [项目工作流](../../docs/PROJECT_WORKFLOW.md)。

## Links

- [Go 参考实现目录](third_party__shmipc-go.md)
- [架构概要](../01_OVERVIEW.md)
