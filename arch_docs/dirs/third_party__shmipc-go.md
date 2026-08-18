# `third_party/shmipc-go/`

## Summary

**职责**：固定的 Go 行为参考、跨语言互操作对端和兼容测试 oracle。它不应成为 C++ 库的编译依赖。

**入口文件**：

- `session.go`：基础 Session API 与多路复用主入口。
- `listener.go`：低层异步服务端入口。
- `session_manager.go`：多 Session 客户端入口。
- `net_listener.go`：标准 `net.Listener/net.Conn` 适配入口。

**依赖**：Go 标准库、`golang.org/x/sys/unix`、ByteDance gopkg、gopsutil、testify。

## Directory Contents（深度=1）

| 路径 | 类型 | 状态 | 说明 |
|---|---|---|---|
| `.github/` | 目录 | ✅ | Linux 测试、benchmark、兼容矩阵和 lint |
| `example/` | 目录 | ⏸️ | hello world、同步/异步最佳实践和热重启示例 |
| `.gitignore`, `.golangci.yaml`, `.licenserc.yaml` | 配置 | ✅ | 工具与许可证检查配置 |
| `LICENSE`, `CONTIRBUTING.md` | 文档 | ✅ | Apache-2.0 与贡献说明 |
| `README.md`, `README_CN.md`, `ROADMAP.md` | 文档 | ✅ | 功能、用法、性能数据和上游路线图 |
| `go.mod`, `go.sum` | 构建 | ✅ | Go 1.20 模块和依赖锁定 |
| `const.go`, `config.go`, `errors.go`, `stats.go` | 源码 | ✅ | 常量、配置、错误和指标 |
| `protocol_event.go`, `protocol_initializer.go`, `protocol_manager.go` | 源码 | ✅ | 控制协议格式、协商和事件处理 |
| `block_io.go`, `event_dispatcher.go` | 源码 | ✅ | 控制 FD 阻塞握手 IO 与事件抽象 |
| `event_dispatcher_linux.go`, `event_dispatcher_race_linux.go` | 源码 | ✅ | Linux epoll dispatcher 常规/竞态构建版本 |
| `epoll_linux.go`, `epoll_linux_arm64.go` | 源码 | ✅ | amd64/arm64 epoll ABI 适配 |
| `sys_memfd_create_linux.go`, `sys_memfd_create_bsd.go` | 源码 | ✅ | memfd 平台实现/不支持占位 |
| `queue.go` | 源码 | ✅ | 共享内存双向环形队列 |
| `buffer_manager.go`, `buffer_slice.go`, `buffer.go` | 源码 | ✅ | 分级 buffer pool、slice 链和零拷贝 API |
| `session.go`, `stream.go` | 源码 | ✅ | Session/Stream 状态机和数据路径 |
| `listener.go`, `session_manager.go`, `net_listener.go` | 源码 | ✅ | 服务端、客户端池、兼容适配和热重启 |
| `debug.go`, `util.go` | 源码 | ⏸️ | 日志、共享内存调试和通用辅助函数 |
| `bench_test.go` | 测试 | ✅ | shmipc 与 Unix Socket ping-pong benchmark |
| `block_io_test.go`, `config_test.go`, `debug_test.go`, `event_dispatcher_test.go`, `listener_test.go`, `protocol_manager_test.go`, `util_test.go` | 测试 | ✅ | 控制层、配置、监听和工具测试 |
| `queue_test.go`, `buffer_manager_test.go`, `buffer_slice_test.go`, `buffer_test.go` | 测试 | ✅ | 共享布局、并发、分配回收和 buffer API 测试 |
| `session_test.go`, `session_manager_test.go`, `stream_test.go` | 测试 | ✅ | 多路复用、fallback、池、超时与关闭测试 |

## 测试资产

- 63 个 `Test*`。
- 26 个 `Benchmark*`。
- 上游 CI 主测试环境为 Linux；本项目应复用测试意图，但不能把 Go 私有实现测试机械翻译为 C++。
- 首要复用方式：Go/C++ 双进程互操作测试、byte-level golden、状态机与资源不变量测试。

## Edge Cases & Gotchas

- 控制消息为大端，共享内存字段为本机字节序。
- arm64 queue header 与 amd64 字段顺序不同，以满足 64 位原子对齐。
- `BufferReader.ReadBytes/Peek` 的结果生命周期依赖 `ReleasePreviousRead`。
- Stream fallback 是粘性的，不能在同一 Stream 恢复共享内存发送。
- 共享内存耗尽会触发 fallback 并令 Session 短暂 unhealthy。
- 服务端的 Stream 是收到首个 opened 数据后才创建，而非显式 open 帧。

## Links

- [架构概要](../01_OVERVIEW.md)
- [协议常量](../files/third_party__shmipc-go__const.go.md)
- [控制事件](../files/third_party__shmipc-go__protocol_event.go.md)
- [关系图](../graphs/relations.md)
