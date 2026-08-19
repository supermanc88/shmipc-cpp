# 架构文档索引

## 文档结构

- [01_OVERVIEW.md](01_OVERVIEW.md)：上游 Go 实现的顶层架构概要
- [02_DECISIONS.md](02_DECISIONS.md)：已验证结论、风险与待决策项
- [dirs/root.md](dirs/root.md)：当前 C++ 工程根目录
- [dirs/src__core.md](dirs/src__core.md)：C++ v2 握手与跨子系统组合层
- [dirs/src__protocol.md](dirs/src__protocol.md)：C++ 控制协议生产 codec
- [dirs/src__shm.md](dirs/src__shm.md)：C++ 共享内存显式布局访问器
- [dirs/src__transport.md](dirs/src__transport.md)：Unix/TCP 控制 socket 与事件传输层
- [dirs/tools__go_oracle.md](dirs/tools__go_oracle.md)：固定 Go 协议/数据平面 oracle
- [files/src__protocol__control_codec.hpp.md](files/src__protocol__control_codec.hpp.md)：生产 codec 完整接口与错误模型
- [files/src__shm__queue_layout.hpp.md](files/src__shm__queue_layout.hpp.md)：queue 布局常量、类型和完整接口
- [files/src__shm__buffer_layout.hpp.md](files/src__shm__buffer_layout.hpp.md)：buffer 布局常量、角色 counter 与完整接口
- [files/src__shm__shared_memory_region.hpp.md](files/src__shm__shared_memory_region.hpp.md)：file/memfd RAII、错误与所有权接口
- [files/src__shm__buffer_pool.hpp.md](files/src__shm__buffer_pool.hpp.md)：跨进程原子 pool、链式 slice 与完整接口
- [files/src__shm__buffer_io.hpp.md](files/src__shm__buffer_io.hpp.md)：连续 Buffer IO、零拷贝与 pin/release 生命周期
- [files/src__shm__atomic_word.hpp.md](files/src__shm__atomic_word.hpp.md)：共享 32/64 位原子 primitive
- [files/src__shm__shared_queue.hpp.md](files/src__shm__shared_queue.hpp.md)：MPSC queue 与 working flag
- [files/src__transport__control_socket.hpp.md](files/src__transport__control_socket.hpp.md)：move-only socket/listener 与 exact IO
- [files/src__transport__epoll_dispatcher.hpp.md](files/src__transport__epoll_dispatcher.hpp.md)：Linux epoll、读缓冲、写背压与关闭生命周期
- [files/src__core__v2_handshake.hpp.md](files/src__core__v2_handshake.hpp.md)：v2 握手状态、资源 ownership 与错误模型
- [files/src__core__v2_client_session.hpp.md](files/src__core__v2_client_session.hpp.md)：v2 client 单 Session/Stream 数据路径
- [files/src__core__v2_server_session.hpp.md](files/src__core__v2_server_session.hpp.md)：v2 server 动态绑定单 Stream 数据路径
- [files/src__core__v2_multiplexed_session.hpp.md](files/src__core__v2_multiplexed_session.hpp.md)：v2 多路 Session 路由与独立 Stream 状态
- [files/src__core__protocol_version_negotiation.hpp.md](files/src__core__protocol_version_negotiation.hpp.md)：v3 版本协商角色状态、降级结果与错误模型
- [files/src__core__v3_handshake.hpp.md](files/src__core__v3_handshake.hpp.md)：完整 v3 memfd/SCM_RIGHTS 握手与共享资源 ownership
- [dirs/third_party__shmipc-go.md](dirs/third_party__shmipc-go.md)：Go 参考实现文件映射
- [files/third_party__shmipc-go__const.go.md](files/third_party__shmipc-go__const.go.md)：协议与布局常量
- [files/third_party__shmipc-go__protocol_event.go.md](files/third_party__shmipc-go__protocol_event.go.md)：控制协议事件格式
- [graphs/relations.md](graphs/relations.md)：运行时边界与关键链路
- [../docs/SHMIPC_CPP_PORTING_PLAN.md](../docs/SHMIPC_CPP_PORTING_PLAN.md)：C++ 移植执行计划
- [../docs/PROJECT_WORKFLOW.md](../docs/PROJECT_WORKFLOW.md)：本项目标准开发、远程构建与验收工作流

## 目录映射

| 路径 | 文档 | 状态 | 标签 | 说明 |
|---|---|---|---|---|
| `/` | [dirs/root.md](dirs/root.md) | ✅ | #cpp-root #bootstrap | C++17/CMake 最小工程骨架，S-0001 已验收 |
| `.github/workflows/` | [dirs/root.md](dirs/root.md) | ✅ | #ci #linux | Linux 编译、安装和 Sanitizer 门禁 |
| `arch_docs/` | 本索引 | ✅ | #architecture | 可持续更新的架构记忆 |
| `cmake/` | [dirs/root.md](dirs/root.md) | ✅ | #build #install | 编译选项、Sanitizer 和 CMake package 配置 |
| `docs/` | [移植计划](../docs/SHMIPC_CPP_PORTING_PLAN.md)、[项目工作流](../docs/PROJECT_WORKFLOW.md) | ✅ | #plan #workflow | 需求、里程碑、门禁与远程验证流程 |
| `include/shmipc/` | [dirs/root.md](dirs/root.md) | ✅ | #public-api | 公共 C++ 头文件入口 |
| `src/` | [dirs/root.md](dirs/root.md) | ✅ | #implementation | C++ 库实现入口 |
| `src/core/` | [dirs/src__core.md](dirs/src__core.md) | ✅ | #handshake #v2 #v3 #interop | v2 文件握手及完整 v3 memfd 资源握手已完成双向验证 |
| `src/protocol/` | [dirs/src__protocol.md](dirs/src__protocol.md) | ✅ | #protocol #codec #safety | header、metadata 与 fallback 生产编解码 |
| `src/shm/` | [dirs/src__shm.md](dirs/src__shm.md) | ✅ | #shared-memory #layout #mmap #zero-copy | 显式布局、mapping、pool、queue 与 Buffer IO |
| `src/transport/` | [dirs/src__transport.md](dirs/src__transport.md) | ✅ | #transport #socket #epoll | Unix/TCP、SCM_RIGHTS 与 Linux epoll 已完成本机/远端验证 |
| `tests/` | [dirs/root.md](dirs/root.md) | ✅ | #tests | CTest 自动测试入口 |
| `tools/go_oracle/` | [dirs/tools__go_oracle.md](dirs/tools__go_oracle.md) | ✅ | #go #oracle #golden | 固定 commit 校验与协议/数据平面 oracle |
| `third_party/` | — | ✅ | #third-party | 外部参考实现聚合目录 |
| `third_party/shmipc-go/` | [dirs/third_party__shmipc-go.md](dirs/third_party__shmipc-go.md) | ✅ | #go #reference #oss | 用户明确要求分析的固定上游源码 |
| `third_party/shmipc-go/.github/` | — | ✅ | #ci | Go 测试与 lint 工作流 |
| `third_party/shmipc-go/example/` | — | ⏸️ | #examples | 已确认同步、异步、net.Conn 和热重启用法，未逐文件详解 |

## 关键文件索引

| 路径 | 状态 | 标签 | 说明 |
|---|---|---|---|
| `.github/workflows/ci.yml` | ✅ | #ci #gcc #clang #sanitizer | Ubuntu 24.04 Debug/Release 与 Sanitizer 矩阵 |
| `CMakeLists.txt` | ✅ | #cmake #install | C++17 library、CTest、安装与 package export |
| `cmake/ShmipcProjectOptions.cmake` | ✅ | #warnings #sanitizer | GCC/Clang/MSVC 告警策略及 ASan/UBSan/TSan 入口 |
| `include/shmipc/version.hpp` | ✅ | #public-api #version | 当前最小公共 API 和版本声明 |
| `src/version.cpp` | ✅ | #implementation #version | 版本 API 实现 |
| `src/protocol/control_codec.hpp` | ✅ | #protocol #codec #errors | 生产 codec 类型、常量和完整接口 |
| `src/protocol/control_codec.cpp` | ✅ | #protocol #codec #big-endian | 显式大端编解码与帧边界验证 |
| `src/shm/queue_layout.hpp` | ✅ | #shared-memory #layout #errors | queue 布局常量、类型与完整访问接口 |
| `src/shm/queue_layout.cpp` | ✅ | #shared-memory #layout #bounds | native-endian memcpy 访问与边界校验 |
| `src/shm/buffer_layout.hpp` | ✅ | #shared-memory #buffer #layout | manager/list/slice 类型与角色 counter API |
| `src/shm/buffer_layout.cpp` | ✅ | #shared-memory #buffer #bounds | buffer 布局显式访问与 checked size |
| `src/shm/shared_memory_region.hpp` | ✅ | #shared-memory #raii #ownership | move-only mapping、FD/path 所有权与错误接口 |
| `src/shm/shared_memory_region.cpp` | ✅ | #mmap #memfd #file | MAP_SHARED 创建、映射与清理实现 |
| `src/shm/buffer_pool.hpp` | ✅ | #shared-memory #allocator #ownership | 原子 pool、move-only token 与 chain API |
| `src/shm/buffer_pool.cpp` | ✅ | #buffer #free-list #atomic | CAS 分配回收、publish/adopt 与严格校验 |
| `src/shm/buffer_io.hpp` | ✅ | #buffer #zero-copy #lifetime | Writer/Reader、view、pin/release 与错误接口 |
| `src/shm/buffer_io.cpp` | ✅ | #buffer #copy #raii | 档位写入、单片零拷贝、跨片复制与回收 |
| `src/shm/atomic_word.hpp` | ✅ | #atomic #cross-process #seq-cst | lock-free 32/64 位共享原子 primitive |
| `src/shm/shared_queue.hpp` | ✅ | #queue #mpsc #working | MPSC put/pop、batch 与 working flag 接口 |
| `src/shm/shared_queue.cpp` | ✅ | #queue #atomic #interop | 本地 producer mutex、共享原子及唤醒状态机 |
| `src/transport/control_socket.hpp` | ✅ | #transport #ownership #io | socket/listener ownership、错误和工厂接口 |
| `src/transport/control_socket.cpp` | ✅ | #tcp #unix #posix | connect/listen/accept、exact IO 与路径清理 |
| `src/transport/epoll_dispatcher.hpp` | ✅ | #transport #epoll #lifecycle | dispatcher、event connection、callback 与关闭原因接口 |
| `src/transport/epoll_dispatcher.cpp` | ✅ | #linux #epoll #backpressure | ET 读、EPOLLOUT 等待、eventfd 停止与关闭串行化 |
| `src/core/v2_handshake.hpp` | ✅ | #handshake #ownership #errors | v2 配置、结果与 move-only 共享资源聚合 |
| `src/core/v2_handshake.cpp` | ✅ | #handshake #metadata #mapping | client 创建/发送与 server 接收/映射状态机 |
| `src/core/protocol_version_negotiation.hpp` | ✅ | #handshake #v3 #version | 双角色协商结果、降级语义与细分错误接口 |
| `src/core/protocol_version_negotiation.cpp` | ✅ | #handshake #v3 #interop | 8 字节 ExchangeProtoVersion 阻塞状态机 |
| `src/core/v3_handshake.hpp` | ✅ | #handshake #v3 #ownership | memfd 配置、资源聚合与细分错误接口 |
| `src/core/v3_handshake.cpp` | ✅ | #handshake #v3 #scm-rights | metadata、ACK、FD 传递与共享资源映射状态机 |
| `src/core/v2_client_session.hpp` | ✅ | #session #stream #errors | 单 client Session/Stream API 与错误模型 |
| `src/core/v2_client_session.cpp` | ✅ | #epoll #queue #buffer | Polling、消息收发、timeout 与 close 状态机 |
| `src/core/v2_server_session.hpp` | ✅ | #server #session #stream | 首个远端 Stream 动态绑定与服务端 API |
| `tests/version_test.cpp` | ✅ | #test | 无第三方依赖的首个 library test |
| `tests/control_header_golden_test.cpp` | ✅ | #test #protocol #golden | C++ 侧消费 control-header fixture |
| `tests/protocol_codec_test.cpp` | ✅ | #test #protocol #negative | metadata/fallback round-trip 与异常输入测试 |
| `tests/queue_layout_test.cpp` | ✅ | #test #layout #amd64 #arm64 | 双架构 queue golden 与异常输入测试 |
| `tests/shared_queue_test.cpp` | ✅ | #test #mpsc #cross-process | 并发生产消费、环绕和 working 竞争测试 |
| `tests/shared_queue_interop_helper.cpp` | ✅ | #test #interop #queue | Go oracle 调用的双向 queue helper |
| `tests/buffer_layout_test.cpp` | ✅ | #test #layout #buffer | manager/list/slice golden 与错误路径 |
| `tests/shared_memory_region_test.cpp` | ✅ | #test #mmap #memfd | 双视图、move、unlink 与 FD ownership 测试 |
| `tests/buffer_pool_test.cpp` | ✅ | #test #allocator #corruption | 档位回退、角色 ownership、耗尽回收与损坏 header |
| `tests/buffer_io_test.cpp` | ✅ | #test #zero-copy #lifetime | Writer/Reader、跨片慢路径、pin/release 与 RAII |
| `tests/control_socket_test.cpp` | ✅ | #test #transport #socket | partial IO、EOF、would-block、TCP/Unix 与清理 |
| `tests/epoll_dispatcher_test.cpp` | ✅ | #test #linux #concurrency | partial frame、背压、并发写、callback/close 与资源上限 |
| `tests/v2_handshake_test.cpp` | ✅ | #test #handshake #cleanup | v2 成功、方向、错误帧、mapping 与事务清理 |
| `tests/v2_handshake_interop_helper.cpp` | ✅ | #test #interop #v2 | 固定 Go oracle 调用的双向握手 helper |
| `tests/protocol_version_negotiation_test.cpp` | ✅ | #test #handshake #v3 | 协商成功、降级、未来版本与异常帧矩阵 |
| `tests/protocol_version_negotiation_interop_helper.cpp` | ✅ | #test #interop #v3 | 固定 Go oracle 调用的双向版本协商 helper |
| `tests/v3_handshake_test.cpp` | ✅ | #test #handshake #v3 | 完整资源握手、异常回滚与 FD 泄漏检查 |
| `tests/v3_handshake_interop_helper.cpp` | ✅ | #test #interop #v3 | 固定 Go newSession 调用的双向完整握手 helper |
| `tests/v2_client_session_test.cpp` | ✅ | #test #session #roundtrip | 单 Stream 跨 slice 双向消息、timeout 与 close |
| `tests/v2_client_session_interop_helper.cpp` | ✅ | #test #interop #stream | C++ client→真实 Go server helper |
| `tests/buffer_pool_interop_helper.cpp` | ✅ | #test #interop #chain | Go oracle 调用的 C++ 双向 chain helper |
| `tests/data/golden/control_headers.txt` | ✅ | #protocol #golden | 事件 0..9 的 8 字节控制头基线 |
| `tests/data/golden/shm_metadata.txt` | ✅ | #protocol #golden | v2 文件路径与 v3 memfd metadata 基线 |
| `tests/data/golden/fallback_data.txt` | ✅ | #protocol #golden | fallback stream/status/payload 基线 |
| `tests/data/golden/queue_layout.txt` | ✅ | #queue #golden #layout | amd64/arm64 queue header 与 element byte 基线 |
| `tests/data/golden/buffer_layout.txt` | ✅ | #buffer #golden #layout | manager/list/slice offsets 与角色 counter 基线 |
| `tests/data/corpus/layout_corruption.txt` | ✅ | #buffer #corpus #safety | 9 类截断/溢出/offset/链损坏输入 |
| `tools/go_oracle/run_control_header_oracle.go` | ✅ | #go #oracle | 固定 commit 检查与无侵入 overlay runner |
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
| v2 `/dev/shm` 握手 | `src/core/v2_handshake.*`, `protocol_initializer.go`, `protocol_manager.go` | `COMP-001`, `PROTO-002` |
| 控制协议 codec 与 golden/oracle | `src/protocol/`, `tools/go_oracle/`, `tests/data/golden/` | `PROTO-001` |
| v3 版本协商 | `src/core/protocol_version_negotiation.*`, `protocol_initializer.go`, `protocol_manager.go` | `COMP-002`, `PROTO-002` |
| v3 `memfd` + SCM_RIGHTS | `src/core/v3_handshake.*`, `src/transport/control_socket.*`, `protocol_initializer.go`, `protocol_manager.go` | `COMP-002`, `PLAT-002` |
| 共享内存分配与回收 | `buffer_manager.go`, `buffer_slice.go`, `buffer.go` | `SHM-001..004` |
| C++ buffer layout 与损坏链验证 | `src/shm/buffer_layout.*`, `tests/data/golden/buffer_layout.txt`, `tests/data/corpus/layout_corruption.txt` | `SHM-001`, `SHM-004` |
| C++ file/memfd mapping | `src/shm/shared_memory_region.*`, `tests/shared_memory_region_test.cpp` | `SHM-001`, `PLAT-002` |
| C++ 分级 buffer 分配回收 | `src/shm/buffer_pool.*`, `tests/buffer_pool_test.cpp` | `SHM-002` |
| C++ 连续 Buffer IO 与零拷贝生命周期 | `src/shm/buffer_io.*`, `tests/buffer_io_test.cpp` | `SHM-003` |
| Go↔C++ 链式 slice 互操作 | `tests/buffer_pool_interop_helper.cpp`, `tools/go_oracle/control_header_oracle_test.gotxt` | `SHM-003` |
| 批量 IO 队列 | `queue.go`, `session.go`, `protocol_manager.go` | `QUEUE-001..003` |
| C++ queue layout accessors | `src/shm/queue_layout.*`, `tests/data/golden/queue_layout.txt` | `QUEUE-001..003` |
| C++ MPSC queue 与 Go 互操作 | `src/shm/shared_queue.*`, `tests/shared_queue*_helper.cpp`, `tools/go_oracle/` | `QUEUE-001..003` |
| Unix/TCP 控制连接与 Linux 事件层 | `src/transport/control_socket.*`, `src/transport/epoll_dispatcher.*`, `tests/*transport*`, `tests/epoll_dispatcher_test.cpp` | `COMP-001`, `S-0301` |
| Stream 多路复用 | `session.go`, `stream.go`, `src/core/v2_multiplexed_session.*` | `STREAM-001..004` |
| C++ v2 单 Stream 双角色 | `src/core/v2_{client,server}_session.*`, `tests/v2_*_session*` | `COMP-001`, `STREAM-001..002` |
| C++ v2 client-originated 多 Stream | `src/core/v2_multiplexed_session.*`, `tests/v2_multiplexed_session*` | `COMP-001`, `STREAM-001..002` |
| 控制通道 sticky fallback | `src/core/v2_multiplexed_session.*`, `stream.go`, `protocol_manager.go` | `STREAM-003` |
| 服务监听和热重启 | `listener.go`, `session_manager.go` | `API-002`, `OPS-001` |
| 性能与稳定性指标 | `stats.go`, `bench_test.go` | `NFR-003`, `OBS-001` |

## 分析进度

- 已完成：上游架构分析、M0、M1、M2，以及 M3 `S-0301..0304`；提交 `0347f34` 的 run `32158446306` 七项门禁全部成功，Go protocol oracle 为 15/15。
- 已完成：`S-0305a/b` 多 Stream、deadline、queue-full retry/close fallback、路由回收和 Session 错误扇出已通过提交 `78913e6` 的云端七项门禁，M3 正式关闭。
- 已完成：`S-0401` 的提交 `807b4fa` 经 GitHub Actions run `32207020590` 七项门禁通过。
- 已完成：`S-0402` 的提交 `568817c` 经 GitHub Actions run `32209295664` 七项门禁通过。
- 进行中：`S-0403a` 多路 Session 数据 fallback 与 per-Stream sticky ordering 已通过本机三套专项、远端 Debug/ASan、固定 Go 双向普通 50 轮及 ASan 10 轮；待完整门禁与提交。
- 部分完成：示例和热重启仅分析到架构/调用层；debug、日志和工具函数未逐符号记录。
- 待验证：将已验证的 fallback 数据面接入 v3 资源、fallback/close 跨通道边界和更完整的异常注入矩阵。v3 握手、数据 sticky fallback、版本协商及多 Stream 生命周期已完成本机和远端验证。

## 状态标记

- ✅ 已分析
- ⏸️ 部分分析或仍需运行时验证
- ❌ 未分析
- 🚫 已跳过
