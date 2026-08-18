# 项目根目录 `/`

## Summary

当前仓库已建立 C++17/CMake 可验证骨架，并已加入控制协议 codec、共享布局、RAII mapping、跨进程原子 buffer pool、零拷贝 Buffer IO、MPSC queue，以及 Unix/TCP control socket 与 Linux epoll 事件层。

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
| `src/` | 目录 | ✅ | library 实现；包含版本、protocol、shared-memory 与 transport 模块 |
| `tests/` | 目录 | ✅ | CTest 测试 target |
| `third_party/` | 目录 | ✅ | 上游参考实现聚合目录 |
| `tools/` | 目录 | ✅ | Go oracle 等开发验证工具 |

## Evidence

- `.gitmodules` 将 Go 参考实现指向 `https://github.com/cloudwego/shmipc-go.git`。
- `CMakeLists.txt` 声明 C++17 target `shmipc` 与 alias `shmipc::shmipc`，并导出 CMake package。
- `tests/version_test.cpp` 通过公开头调用 library 实现，构成首个端到端链接测试。
- `.github/workflows/ci.yml:18-89` 定义 GCC/Clang Debug/Release 常规矩阵及 ASan+UBSan/TSan 独立矩阵。
- 2026-08-18：本机 Debug/test/install 与 ASan+UBSan 通过；远端 Linux GCC 8.5 Debug/test/install 及独立 ASan 通过，UBSan/TSan 仍缺远端运行库。
- 2026-08-18：提交 `eeae84e` 的 GitHub Actions run `32116398237` 中，GCC/Clang Debug/Release、ASan+UBSan 和 TSan 六项全部通过。
- `tools/go_oracle/run_control_header_oracle.go:31-68` 校验固定 commit，并以临时 overlay 调用真实上游编码器；`tests/control_header_golden_test.cpp` 消费同一 fixture。
- 2026-08-18：提交 `34ef510` 的 GitHub Actions run `32119710781` 七项作业全部通过，包括独立 Go control-header oracle。
- `src/protocol/control_codec.cpp:110-275` 实现 header、metadata 与 fallback 编解码；`tests/protocol_codec_test.cpp:46-199` 验证 golden round-trip 与异常输入。
- 2026-08-18：`S-0101` 在本机 AppleClang Debug/ASan+UBSan 和远端 GCC 8.5 Debug/ASan 下通过。
- 2026-08-18：提交 `603933e` 的 GitHub Actions run `32122127419` 七项作业全部成功，`S-0101` 完成。
- `src/shm/queue_layout.cpp:81-192` 实现双架构偏移、region size 和 header/element 边界访问；`tests/queue_layout_test.cpp:48-175` 验证两套 golden 与错误路径。
- `src/shm/buffer_layout.cpp:82-225` 实现 manager/list/slice 布局；Go 双视图 probe 证明 `+20/+24` 是 creator/mapper 独立 counter。
- 提交 `ed4c7a8` 的 GitHub Actions run `32125329954` 七项作业全部成功，M1 完成。
- `src/shm/shared_memory_region.cpp:100-315` 实现 move-only file/memfd mapping；`tests/shared_memory_region_test.cpp:21-137` 已在本机 AppleClang 和远端 GCC 8.5 Debug/ASan 通过。
- `src/shm/atomic_word.hpp` 与 `buffer_pool.cpp:36-525` 实现 lock-free seq_cst 分配回收和 chain publish/adopt；父子进程压力、Sanitizer 及双向 Go oracle 通过。
- `src/shm/shared_queue.cpp:74-173` 实现 MPSC put/pop、batch 与 working flag；本机/远端压力、Sanitizer 及双向 Go oracle 通过。
- `src/shm/buffer_io.cpp:42-491` 实现 Writer/Reader、单片零拷贝、跨片复制和 pin/release；本机 oracle/ASan+UBSan/TSan 与远端 GCC 8.5 Debug/ASan 通过。
- 提交 `c1c23f9` 的 GitHub Actions run `32134325132` 中 GCC/Clang Debug/Release、ASan+UBSan、TSan 和 Go protocol oracle 七项全部成功，M2 完成。
- `src/transport/control_socket.cpp:18-405` 实现 move-only FD、Unix/TCP connect/listen/accept 与 exact IO；本机三套配置及远端 GCC 8.5 Debug/ASan 10/10 通过。
- `src/transport/epoll_dispatcher.cpp:68-595` 实现 edge-triggered epoll、消费式读缓冲、串行写背压、eventfd 停止与唯一关闭回调；远端 GCC 8.5 Debug/Release/ASan 11/11、专项 100 次通过。
- 提交 `17a668e` 的 GitHub Actions run `32148166394` 中 GCC/Clang Debug/Release、ASan+UBSan、TSan 与 Go protocol oracle 七项全部成功，`S-0301` 关闭。

## Guesses & Uncertainties

- 当前产物为静态库；最终是否同时发布动态库、依赖策略和完整公共 API 仍待确认。
- 推荐目录和里程碑见 [移植计划](../../docs/SHMIPC_CPP_PORTING_PLAN.md)。
- 远程 Linux 同步、构建和测试见 [项目工作流](../../docs/PROJECT_WORKFLOW.md)。

## Links

- [Go 参考实现目录](third_party__shmipc-go.md)
- [Go oracle 目录](tools__go_oracle.md)
- [架构概要](../01_OVERVIEW.md)
