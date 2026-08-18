# 项目根目录 `/`

## Summary

当前仓库已建立 C++17/CMake 最小可构建骨架：包含静态 library target、公共版本 API、CTest、编译告警/Sanitizer 选项以及安装导出。协议与共享内存实现尚未开始。

## Directory Contents（深度=1）

| 路径 | 类型 | 状态 | 说明 |
|---|---|---|---|
| `.github/workflows/` | 目录 | ✅ | Ubuntu Linux 编译、测试、安装与 Sanitizer CI |
| `.gitmodules` | 文件 | ✅ | 声明 `third_party/shmipc-go` 子模块 |
| `CMakeLists.txt` | 文件 | ✅ | 顶层工程、library/test、安装与 package export |
| `README.md` | 文件 | ✅ | 项目定位和唯一构建入口 |
| `arch_docs/` | 目录 | ✅ | 架构分析与证据索引 |
| `cmake/` | 目录 | ✅ | 工程选项与 package config 模板 |
| `docs/` | 目录 | ✅ | C++ 移植计划与项目标准工作流 |
| `include/shmipc/` | 目录 | ✅ | 公共 C++ API；当前为版本接口 |
| `src/` | 目录 | ✅ | library 实现；当前为版本接口 |
| `tests/` | 目录 | ✅ | CTest 测试 target |
| `third_party/` | 目录 | ✅ | 上游参考实现聚合目录 |

## Evidence

- `.gitmodules` 将 Go 参考实现指向 `https://github.com/cloudwego/shmipc-go.git`。
- `CMakeLists.txt` 声明 C++17 target `shmipc` 与 alias `shmipc::shmipc`，并导出 CMake package。
- `tests/version_test.cpp` 通过公开头调用 library 实现，构成首个端到端链接测试。
- `.github/workflows/ci.yml:18-89` 定义 GCC/Clang Debug/Release 常规矩阵及 ASan+UBSan/TSan 独立矩阵。
- 2026-08-18：本机 Debug/test/install 与 ASan+UBSan 通过；远端 Linux GCC 8.5 Debug/test/install 及独立 ASan 通过，UBSan/TSan 仍缺远端运行库。

## Guesses & Uncertainties

- 当前产物为静态库；最终是否同时发布动态库、依赖策略和完整公共 API 仍待确认。
- 推荐目录和里程碑见 [移植计划](../../docs/SHMIPC_CPP_PORTING_PLAN.md)。
- 远程 Linux 同步、构建和测试见 [项目工作流](../../docs/PROJECT_WORKFLOW.md)。

## Links

- [Go 参考实现目录](third_party__shmipc-go.md)
- [架构概要](../01_OVERVIEW.md)
