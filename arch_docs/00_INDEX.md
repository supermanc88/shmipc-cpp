# 架构文档索引

## 文档结构

- [01_OVERVIEW.md](01_OVERVIEW.md)：上游 Go 实现的顶层架构概要
- [02_DECISIONS.md](02_DECISIONS.md)：已验证结论、风险与待决策项
- [dirs/root.md](dirs/root.md)：当前 C++ 工程根目录
- [dirs/third_party__shmipc-go.md](dirs/third_party__shmipc-go.md)：Go 参考实现文件映射
- [files/third_party__shmipc-go__const.go.md](files/third_party__shmipc-go__const.go.md)：协议与布局常量
- [files/third_party__shmipc-go__protocol_event.go.md](files/third_party__shmipc-go__protocol_event.go.md)：控制协议事件格式
- [graphs/relations.md](graphs/relations.md)：运行时边界与关键链路
- [../docs/SHMIPC_CPP_PORTING_PLAN.md](../docs/SHMIPC_CPP_PORTING_PLAN.md)：C++ 移植执行计划
- [../docs/PROJECT_WORKFLOW.md](../docs/PROJECT_WORKFLOW.md)：本项目标准开发、远程构建与验收工作流

## 目录映射

| 路径 | 文档 | 状态 | 标签 | 说明 |
|---|---|---|---|---|
| `/` | [dirs/root.md](dirs/root.md) | ✅ | #cpp-root #bootstrap | C++ 工程根目录，当前尚未建立源码骨架 |
| `arch_docs/` | 本索引 | ✅ | #architecture | 可持续更新的架构记忆 |
| `docs/` | [移植计划](../docs/SHMIPC_CPP_PORTING_PLAN.md)、[项目工作流](../docs/PROJECT_WORKFLOW.md) | ✅ | #plan #workflow | 需求、里程碑、门禁与远程验证流程 |
| `third_party/` | — | ✅ | #third-party | 外部参考实现聚合目录 |
| `third_party/shmipc-go/` | [dirs/third_party__shmipc-go.md](dirs/third_party__shmipc-go.md) | ✅ | #go #reference #oss | 用户明确要求分析的固定上游源码 |
| `third_party/shmipc-go/.github/` | — | ✅ | #ci | Go 测试与 lint 工作流 |
| `third_party/shmipc-go/example/` | — | ⏸️ | #examples | 已确认同步、异步、net.Conn 和热重启用法，未逐文件详解 |

## 关键文件索引

| 路径 | 状态 | 标签 | 说明 |
|---|---|---|---|
| `third_party/shmipc-go/const.go` | ✅ | #protocol #constants | 协议版本、共享内存模式、默认容量和头部尺寸 |
| `third_party/shmipc-go/protocol_event.go` | ✅ | #wire-format | 8 字节大端控制头和事件类型 |
| `third_party/shmipc-go/protocol_initializer.go` | ✅ | #handshake | v2/v3 初始化状态机 |
| `third_party/shmipc-go/protocol_manager.go` | ✅ | #protocol #fallback | 事件分派、共享内存交换、memfd 传递、fallback |
| `third_party/shmipc-go/queue.go` | ✅ | #shared-memory #queue | 双向 MPSC 队列及跨进程唤醒标志 |
| `third_party/shmipc-go/buffer_manager.go` | ✅ | #shared-memory #allocator | 分级共享内存 buffer pool |
| `third_party/shmipc-go/buffer_slice.go` | ✅ | #buffer-layout | 20 字节 slice header 和链表语义 |
| `third_party/shmipc-go/buffer.go` | ✅ | #zero-copy | BufferReader/Writer 及 pin/recycle 生命周期 |
| `third_party/shmipc-go/session.go` | ✅ | #session #multiplexing | 连接握手、多路复用、发送循环、队列唤醒 |
| `third_party/shmipc-go/stream.go` | ✅ | #stream #api | Stream 状态机、同步/异步接口、fallback 保序 |
| `third_party/shmipc-go/listener.go` | ✅ | #server #hot-restart | 低层异步 Listener 和服务端热重启 |
| `third_party/shmipc-go/session_manager.go` | ✅ | #client #pool | 多 Session、Stream 复用、断线重建和热重启 |
| `third_party/shmipc-go/net_listener.go` | ✅ | #adapter | 面向 net.Listener/net.Conn 的兼容层 |
| `third_party/shmipc-go/event_dispatcher_linux.go` | ✅ | #linux #epoll | 边沿触发 epoll 控制连接事件循环 |

## 功能定位索引

| 功能/意图 | 主要实现 | 计划需求 |
|---|---|---|
| v2 `/dev/shm` 握手 | `protocol_initializer.go`, `protocol_manager.go` | `COMP-001`, `PROTO-001` |
| v3 `memfd` + SCM_RIGHTS | `protocol_initializer.go`, `protocol_manager.go`, `block_io.go` | `COMP-002`, `PLAT-002` |
| 共享内存分配与回收 | `buffer_manager.go`, `buffer_slice.go`, `buffer.go` | `SHM-001..004` |
| 批量 IO 队列 | `queue.go`, `session.go`, `protocol_manager.go` | `QUEUE-001..003` |
| Stream 多路复用 | `session.go`, `stream.go` | `STREAM-001..004` |
| 控制通道 fallback | `stream.go`, `protocol_manager.go` | `STREAM-003` |
| 服务监听和热重启 | `listener.go`, `session_manager.go` | `API-002`, `OPS-001` |
| 性能与稳定性指标 | `stats.go`, `bench_test.go` | `NFR-003`, `OBS-001` |

## 分析进度

- 已完成：顶层模块、协议头、握手、共享内存布局、队列、buffer 生命周期、Session/Stream 主链路、公开 API、测试与 CI 基线。
- 部分完成：示例和热重启仅分析到架构/调用层；debug、日志和工具函数未逐符号记录。
- 待验证：C++ 与 Go 的双向互操作、共享内存原子内存序、`bufferList.counter` 偏移差异。固定 Go 基线已在远程 Linux x86_64 主机完整通过。

## 状态标记

- ✅ 已分析
- ⏸️ 部分分析或仍需运行时验证
- ❌ 未分析
- 🚫 已跳过
