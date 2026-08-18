# ADR-0001：兼容目标与语言基线

- 状态：Accepted
- 日期：2026-08-18

## 背景

项目需要将 `cloudwego/shmipc-go` 重实现为 C++。共享内存布局、控制协议、原子可见性和资源生命周期跨越语言边界，仅做 API 形状相似的翻译无法证明行为兼容。目标 Linux 主机当前提供 GCC 8.5。

## 决策

1. 固定 `third_party/shmipc-go` commit `55c241eea321071278d1ee7f7c46292d23e50a5b` 为开发和验收 oracle。
2. Go↔C++ 双向互操作是协议正确性的最终证据；Go 不是 C++ 产品的运行依赖。
3. 首个平台为 Linux x86_64，核心路径稳定后补 Linux arm64。
4. C++ 最低标准为 C++17，与远端 GCC 8.5 对齐。
5. wire/layout golden 必须由固定 Go 基线验证，C++ 实现消费同一份 fixture。

## 后果

- 升级 Go submodule 必须单独评审并重新生成全部 golden/evidence。
- 公共 API 与内部实现不得依赖 C++20。
- macOS 可用于编辑、编译和快速测试，但 Linux 才是运行时兼容证据环境。
- 第一版共享内存原子操作优先采用保守内存序；优化必须由互操作压力测试证明。

## 已有验证

- 固定 Go 基线完整 Linux/amd64 测试通过。
- C++17 骨架在 AppleClang、远端 GCC 8.5 和 GitHub Actions GCC/Clang 上通过。
- control-header oracle 在运行前校验 submodule commit，并通过 Go overlay 调用上游未导出的 `header.encode`。
