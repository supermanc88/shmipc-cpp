# shmipc-go 架构概要

## 项目基本信息

- 参考实现：`cloudwego/shmipc-go`
- 固定提交：`55c241eea321071278d1ee7f7c46292d23e50a5b`（2025-10-21）
- 语言与基线：Go 1.20；运行目标为 Linux amd64/arm64
- 许可证：Apache-2.0
- 定位：用 Unix Domain Socket 或 TCP 作为可靠有序控制通道，用共享内存承载数据和 IO 元信息，将一条连接多路复用为多个逻辑 Stream。

## 核心判断

这不是把 `mmap` 包成读写 API 的薄层库。完整实现由五个相互约束的子系统构成：

1. 控制协议：协商版本、交换共享内存标识、传递 memfd、唤醒对端、传输 fallback 数据和热重启事件。
2. 共享内存分配器：按 8 KiB、32 KiB、128 KiB 三档管理 buffer slice，并通过共享链表跨进程分配/回收。
3. 双向 IO 队列：每个 Session 有两个环形队列，元素记录 `stream_id + root_buffer_offset + stream_state`。
4. Session/Stream：Session 管理连接、队列、buffer manager 和 Stream 表；Stream 提供零拷贝 Buffer API 及 `net.Conn` 兼容 API。
5. 生命周期与运维：epoll 事件循环、fallback 熔断、指标、连接重建、Stream 池及热重启。

## 模块划分

| 模块 | 文件 | 职责 |
|---|---|---|
| 公共配置/API | `config.go`, `errors.go`, `stats.go` | 配置、错误、指标和公开类型 |
| 控制协议 | `protocol_event.go`, `protocol_initializer.go`, `protocol_manager.go`, `block_io.go` | 线上格式、握手、事件分派、FD 传递 |
| 共享内存队列 | `queue.go` | 两个跨进程 MPSC 环形队列和消费唤醒状态 |
| 共享内存 buffer | `buffer_manager.go`, `buffer_slice.go`, `buffer.go` | 分级分配、slice 链、零拷贝读写和回收 |
| 多路复用 | `session.go`, `stream.go` | Session 生命周期、Stream 状态与数据传递 |
| 服务/客户端管理 | `listener.go`, `session_manager.go`, `net_listener.go` | 监听、连接池、Stream 复用、兼容适配、热重启 |
| Linux 事件层 | `event_dispatcher*.go`, `epoll_linux*.go`, `sys_memfd_create_linux.go` | epoll ET、非阻塞写、memfd 系统调用 |
| 验证资产 | `*_test.go`, `bench_test.go`, `example/` | 63 个 Test、26 个 Benchmark 和多种使用示例 |
| 跨语言 oracle | `tools/go_oracle/`, `tests/data/golden/` | 固定 commit 校验、无侵入 overlay 和共享 byte fixture |

## 运行时边界

每个客户端 Session 创建并拥有一块 buffer 共享内存和一块包含两个队列的共享内存；服务端在握手后映射它们。创建端与映射端对两个队列的 send/recv 视图相反，因此同一块物理内存可承载双向通信。控制连接始终保留，用于通知“队列已有数据”、异常情况下传输 payload，以及关闭/热重启控制。

## 关键链路

### Session 建立

1. `newSession` 校验 Linux/架构/配置并复制底层连接 FD。
2. 客户端创建 buffer manager 和两个共享队列；服务端暂不创建。
3. v2 使用路径直接交换 `/dev/shm` 文件；v3 先协商版本，再通过 Unix Socket 的 `SCM_RIGHTS` 传递两个 memfd。
4. 服务端映射共享内存并确认，双方将控制 FD 注册到默认 epoll dispatcher。
5. Session 启动串行发送循环和可选的 30 秒监控循环。

### 发送数据

1. 应用通过 `BufferWriter.Reserve/WriteBytes` 写入共享内存 slice 链。
2. `Stream.Flush` 更新每个 slice 的共享 header，将根 offset、Stream ID 和状态写入 send queue。
3. `wakeUpPeer` 仅在队列 consumer 从休眠切换为工作时发送 8 字节 Polling 事件，以实现批量收割。
4. 若共享内存不足，Stream 永久进入 fallback 状态，后续该 Stream 都走控制连接，避免共享内存与 socket 两条路径乱序。

### 接收数据

1. epoll 收到控制连接可读事件，`Session.handleEvents` 解析大端控制头。
2. Polling handler 批量 pop recv queue。
3. 根据 Stream ID 获取或在服务端创建 Stream；通过 root offset 还原 slice 链。
4. 数据进入 `pendingData`，同步读者被唤醒，或异步 `OnData` 回调被调度。
5. `ReadBytes/Peek` 返回的共享内存视图在 `ReleasePreviousRead` 前保持 pinned，之后才可回收到共享 pool。

## 数据与协议布局

### 控制协议头（网络大端）

| Offset | 长度 | 字段 |
|---:|---:|---|
| 0 | 4 | 总消息长度 |
| 4 | 2 | Magic `0x7758` |
| 6 | 1 | 协议版本 |
| 7 | 1 | 事件类型 |

### Queue element（共享内存、本机字节序）

| Offset | 长度 | 字段 |
|---:|---:|---|
| 0 | 4 | Stream/sequence ID |
| 4 | 4 | 根 buffer 在共享内存中的 offset |
| 8 | 4 | Stream state（当前使用低 8 位） |

### Buffer slice header（共享内存、本机字节序）

| Offset | 长度 | 字段 |
|---:|---:|---|
| 0 | 4 | capacity |
| 4 | 4 | 有效数据 size |
| 8 | 4 | data start |
| 12 | 4 | next slice offset |
| 16 | 4 | flags：has-next / in-use |

## 并发模型与必须保留的不变量

- 控制连接写操作必须串行化；Go 通过 `writing` CAS、send channel 和 epoll 可写回调协调。
- Queue 是多生产者、单消费者；生产者先完整写 element，再发布 tail；消费者在看到 tail 后读取 element，再推进 head。
- 共享内存中的 32/64 位原子字段必须满足平台对齐要求；amd64 与 arm64 的 queue header 字段偏移不同。
- 一条 Stream 一旦 fallback，后续发送不得返回共享内存路径，否则可能乱序。
- 返回给调用者的零拷贝读视图，在显式 release 前不得回收或解除映射。
- 关闭 Session 前必须终止事件回调、关闭 Stream、回收/放弃 buffer，再解除映射。
- 服务端只在首次收到 `streamOpened` 数据时感知新 Stream；OpenStream 本身不发送独立控制帧。

## 公开 API 面

- 配置：`Config`, `DefaultConfig`, `VerifyConfig`, `MemMapType`, `SizePercentPair`
- 基础会话：`Server`, `Session.OpenStream/AcceptStream/Close/GetMetrics`
- Stream：`BufferWriter`, `BufferReader`, `Stream`, `StreamCallbacks`
- 服务端：`Listener`, `ListenerConfig`, `ListenCallback`, `NewListener`
- 客户端管理：`SessionManager`, `SessionManagerConfig`, `NewSessionManager`
- 兼容层：`Listen`, `ListenWithBacklog` 返回标准 `net.Listener`
- 可观测性：`Monitor`, `PerformanceMetrics`, `StabilityMetrics`, `ShareMemoryMetrics`

## Build & Deploy

- Go 参考实现构建入口：`go.mod`，Go 1.20。
- C++ 构建入口：根目录 `CMakeLists.txt`，最低 CMake 3.16、C++17；产物 target 为 `shmipc`/`shmipc::shmipc`，支持 CTest、install/export 及 `find_package(shmipc)` package 配置。
- C++ 质量入口：`SHMIPC_WARNINGS_AS_ERRORS`、`SHMIPC_ENABLE_ASAN`、`SHMIPC_ENABLE_UBSAN`、`SHMIPC_ENABLE_TSAN`；ASan 与 TSan 在配置阶段互斥。
- C++ 控制协议入口：`src/protocol/control_codec.hpp`。当前提供 header、事件 0..9、v2/v3 metadata 与 fallback 的大端编解码；以明确错误分类拒绝截断、非法字段、错误事件、尾随字节和超过默认 64 MiB 上限的帧。该接口暂为内部 API。
- C++ queue 布局入口：`src/shm/queue_layout.hpp`。以 `memcpy` 对 mmap 字节做本机字节序访问，显式区分 amd64 与 arm64 header offsets，并在任何字段访问前校验 capacity、region size、slot 和 arm64 manager 对齐；并发原子语义留到 `S-0204`。
- C++ buffer 布局入口：`src/shm/buffer_layout.hpp`。显式定义 8 字节 manager、36 字节 list 与 20 字节 slice header；creator 与 mapper 的 outstanding counters 分别位于 `+20/+24`，普通访问使用 `memcpy` 并校验字段和 region size。
- C++ mapping 入口：`src/shm/shared_memory_region.hpp`。move-only owner 统一管理 `munmap`、memfd descriptor 与创建端路径清理；文件 mapper 不 unlink，memfd API 显式区分 borrowed/transferred descriptor。
- C++ buffer pool 入口：`src/shm/buffer_pool.hpp`。按 capacity 升序管理分级 free lists，保留一个 sentinel，最小合适档位耗尽后向更大档位回退；move-only token 和角色 counter 防止错误回收。当前为单进程非并发实现。
- Go oracle 入口：`go run tools/go_oracle/run_control_header_oracle.go`；严格校验 submodule commit 后，以 overlay 调用上游 header、metadata 与 fallback 编码器核对三份 golden。CMake 可通过 `SHMIPC_ENABLE_GO_ORACLE_TESTS=ON` 将其加入 CTest。
- C++ CI 入口：`.github/workflows/ci.yml`。Ubuntu 24.04 上运行 GCC/Clang × Debug/Release 四项构建、CTest 和安装；另以 GCC 分别运行 ASan+UBSan 与 TSan，并以 Go 1.25.10 运行 control-protocol oracle。
- 本地测试：`go test ./...`；上游测试实际依赖 Linux，macOS 不构成有效通过环境。
- Linux 交叉编译基线：`GOOS=linux GOARCH=amd64 go test -c .` 已在 2026-08-18 成功。
- 远程执行环境：SSH 别名 `23.2`（`root@10.210.23.2`），工作目录 `/home/chm/shmipc-cpp`；Kylin Linux Advanced Server V10、kernel `4.19.90-20.0stable.x86_64`、x86_64。
- 远程工具链：CMake 3.20.6、GCC 8.5.0、Ninja 1.8.2、GNU Make 4.2.1；当前没有 Go、Clang、Docker 或 Podman。
- C++ 骨架验证：macOS/AppleClang 的 Debug、测试、安装和 ASan+UBSan 通过；远端 Linux/GCC 8.5 的 Debug、测试、`lib64` 安装及独立 ASan 构建/测试通过。远端尚缺 `libubsan` 与 `libtsan`，因此 UBSan/TSan 仍属环境阻塞。
- CI 云端验证：提交 `eeae84e` 的首轮 GitHub Actions（run `32116398237`）成功；GCC/Clang × Debug/Release、ASan+UBSan、TSan 六个作业均实际执行并通过，常规四项的安装验证也通过。
- Go oracle 云端验证：提交 `34ef510` 的 GitHub Actions run [`32119710781`](https://github.com/supermanc88/shmipc-cpp/actions/runs/32119710781) 成功；新增 Go 1.25.10 作业及原有六项矩阵共七项全部通过。
- `S-0101` 验证：三份 fixture 同时经固定 Go 编码器与 C++ production codec 验证；本机与远端门禁通过，提交 `603933e` 的 GitHub Actions run [`32122127419`](https://github.com/supermanc88/shmipc-cpp/actions/runs/32122127419) 七项作业全部成功。
- `S-0102` 验证：Go oracle 在 Darwin arm64 与 amd64 运行路径分别验证真实 queue 指针布局；C++ 同时消费两行 golden，远端 GCC 8.5 Debug/ASan 通过；run `32125329954` 云端通过。
- `S-0103` 验证：Go 双视图实验确认 creator/mapper pop 只增加各自 `+20/+24` counter，push 后独立归零；两架构 Go oracle、C++ layout tests 及远端 GCC 8.5 Debug/ASan 通过。
- `S-0104` 验证：buffer free-list validator 以 capacity 限制遍历次数，并通过固定 corpus 分类截断、溢出、非法 offset、cycle、tail/capacity/data-range 损坏；本机、远端及 run `32125329954` 已通过，M1 完成。
- `S-0201` 验证：file/memfd RAII mapping 已通过本机 AppleClang Debug/ASan+UBSan，以及远端 Linux GCC 8.5 Debug/ASan；Linux 测试实际执行 memfd 创建与 FD 借用/转移路径，待批次云端证据。
- `S-0202` 验证：单进程分级 pool 已覆盖乱序配置排序、最小档位选择、耗尽后大档位回退、全部回收、creator/mapper counter、角色错配 token 和损坏 head/tail/size/used-length；本机与远端 Debug/Sanitizer 7/7 通过，待批次云端证据。
- 时钟注意：本机当前比远端快约 2 分 20 秒；同步时不得保留本机文件时间戳，否则 Ninja 会反复重新生成。标准命令见 `PROJECT_WORKFLOW.md`。
- Linux 运行基线：本机用 Go 1.25.10 交叉编译固定提交的 amd64 测试二进制，rsync 至远端后完整测试 `PASS`、退出码 0；覆盖 v2、v3/memfd、队列、Stream/Session 和热重启路径。
- CI：`.github/workflows/tests.yaml` 在 Ubuntu 运行单测/benchmark，并在自托管 Linux 上覆盖 Go 1.21–1.25；`.github/workflows/pre_check.yaml` 运行许可证、拼写和 golangci-lint。
- 运行目标：Linux amd64/arm64；memfd 需要 Linux 3.17+ 且控制连接必须是 Unix Domain Socket。
- 项目远程同步、构建和测试命令见 [PROJECT_WORKFLOW.md](../docs/PROJECT_WORKFLOW.md)。

## 术语表

- 控制连接：Unix/TCP 可靠有序连接，不是只在握手阶段存在。
- Polling event：通知对端开始批量消费共享队列的 8 字节事件，不承载 payload。
- Fallback：共享内存不可用时在控制连接上传输数据的保序降级路径。
- pinned slice：零拷贝结果仍被应用持有、暂不可回收的 slice。
- hot restart：服务端通知客户端用新 epoch 建立替代 Session，确认后切换并关闭旧 Session。
