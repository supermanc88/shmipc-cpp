# shmipc-cpp 项目计划

## 1. 文档信息

| 项目 | 内容 |
|---|---|
| 项目类型 | Go 到 C++ 的跨语言、跨运行时重实现 |
| 参考实现 | `third_party/shmipc-go` commit `55c241eea321071278d1ee7f7c46292d23e50a5b` |
| 当前阶段 | M2 与 M3 `S-0301..0304` 已完成；进入 `S-0305` 多 Stream 与完整关闭语义 |
| 已确认目标 | 在 Linux 上提供现代 C++ 共享内存 IPC 库，并与固定 Go 实现双向互通；Go 仅用于开发验收 |
| 流程依据 | 用户提供的《软件项目端到端标准工作流程》 |
| 架构依据 | [上游架构概要](../arch_docs/01_OVERVIEW.md) 与 [决策/风险](../arch_docs/02_DECISIONS.md) |

## 2. 一句话目标

实现一个可审计、可测试、可发布的 C++ shmipc 库：保持上游控制协议、共享内存布局、Stream 行为和关键性能语义，并用 Go↔C++ 双向互操作证据证明兼容性。

## 3. 范围

### 3.1 当前计划范围

- Linux x86_64 首先可用，随后验证 Linux arm64。
- v2 `/dev/shm` 文件映射模式。
- v3 `memfd` + Unix Domain Socket FD 传递模式。
- 双向共享内存 IO queue 和批量唤醒。
- 分级共享 buffer pool、链式 slice 和显式零拷贝生命周期。
- Session 上的多 Stream 复用、同步读写、deadline、关闭和 fallback。
- 异步 Stream callback、Listener、SessionManager、Stream pool、断线重建和热重启。
- 指标、日志、benchmark、Sanitizer、Linux CI、安装与发布。

### 3.2 首阶段不做

- macOS/Windows 运行支持；它们缺少等价的 epoll/memfd 路径，若未来支持应作为独立架构变更。
- 修改上游 Go 协议或“顺便修复”未验证的布局问题。
- 在协议兼容前进行 lock-free/relaxed memory order 等激进优化。
- 保证跨 CPU 字节序互通；当前共享布局采用本机字节序。

## 4. 启动审计结论

1. 上游包含 26 个生产 Go 文件、15 个根级测试文件、63 个 Test 和 26 个 Benchmark。
2. 控制消息使用 8 字节网络大端头；共享内存中的 queue/buffer 字段直接采用本机字节序。
3. 一个 Session 拥有两个反向映射的队列和一个分级 buffer manager；控制连接负责握手、通知、fallback、关闭及热重启。
4. 默认 `/dev/shm` 路径走 v2；memfd 路径走 v3 协商和 `SCM_RIGHTS`。
5. 上游明确只支持 Linux amd64/arm64。本机 macOS arm64 测试失败，但 Linux/amd64 测试二进制交叉编译成功。
6. `bufferList.counter` 的 `+20/+24` 是 creator/mapper 角色本地 `pop - push` 净计数；字段允许为负，C++ 必须按角色保留。
7. 远程 Linux 主机 `10.210.23.2` 已可通过 SSH 别名 `23.2` 使用；同步目录为 `/home/chm/shmipc-cpp`。
8. 固定 Go 基线的 Linux/amd64 测试二进制已在远端完整运行并 `PASS`。远端当前有 CMake 3.20.6、GCC 8.5.0、Ninja 1.8.2，但没有 Go/Clang/容器运行时。

## 5. 需求矩阵

除下述明确状态外，需求生命周期初始均为“未开始”；决策项确认后才进入实现。

| ID | 要求 | 目标策略 | 优先级 | 验收标准 |
|---|---|---|---|---|
| `COMP-001` | C++ 与 Go v2 `/dev/shm` 双向互通 | 等价迁移 | P0 | Go client↔C++ server、C++ client↔Go server 均完成多 Stream 数据与关闭测试 |
| `COMP-002` | C++ 与 Go v3 memfd 双向互通 | 等价迁移 | P0 | 两个方向完成版本协商、FD 传递、数据、fallback 和关闭测试 |
| `PROTO-001` | 8 字节控制头与事件 0..9 | 等价迁移 | P0 | byte golden 与 Go 编码逐字节一致，异常 length/type/version 被安全拒绝 |
| `PROTO-002` | v2/v3 初始化状态机 | 兼容重构 | P0 | 正常、超时、错误首帧、错误 ACK、FD 数量错误均有自动测试 |
| `SHM-001` | Queue/buffer manager/slice 共享布局 | 等价迁移 | P0 | layout probe 在 amd64/arm64 与 Go 对照一致 |
| `SHM-002` | 分级 buffer 分配与回收 | 兼容重构 | P0 | 单/多线程、双进程压力测试无泄漏、重复分配或链破坏 |
| `SHM-003` | 零拷贝读写与 pin/release 生命周期 | 兼容重构 | P0 | view 在 release 前有效，release 后不再访问；ASan/UBSan/TSan 门禁通过 |
| `SHM-004` | 恶意或损坏 offset/length 防护 | 安全增强 | P0 | 截断、溢出、越界、循环链和超量 slice 被确定性拒绝且不崩溃 |
| `QUEUE-001` | MPSC put/pop 和 full/empty | 等价迁移 | P0 | Go/C++ 并发生产消费不丢、不重、不乱序 |
| `QUEUE-002` | working flag 与批量唤醒 | 等价迁移 | P0 | 空闲→工作只需一次通知，竞争窗口不会遗失唤醒 |
| `QUEUE-003` | amd64/arm64 原子对齐 | 等价迁移 | P0 | 两架构 CI 的 layout/static/runtime 检查通过 |
| `STREAM-001` | Session 多路复用与 Stream ID | 等价迁移 | P0 | 并发多 Stream 创建、收发、回绕/冲突和关闭测试通过 |
| `STREAM-002` | 同步 Buffer API 和 copy API | 兼容重构 | P0 | 正常、跨 slice、partial read、deadline、EOF 行为明确且互通 |
| `STREAM-003` | 共享内存不足时 sticky fallback | 等价迁移 | P0 | 单 Stream 切换后保持 socket 路径且消息顺序不变 |
| `STREAM-004` | 半关闭、关闭、回收和错误语义 | 兼容重构 | P0 | 本地/远端/Session 关闭矩阵无死锁、泄漏或 use-after-free |
| `API-001` | RAII C++ Session/Stream 公共 API | C++ 化 | P0 | 示例可构建；所有权、线程安全、错误模型和 view 生命周期有文档 |
| `API-002` | Listener 和 SessionManager | 兼容重构 | P1 | 多 Session、pool reuse、断线重建通过集成测试 |
| `API-003` | 异步 callback API | 兼容重构 | P1 | callback 串行性、callback 内关闭和析构等待测试通过 |
| `OPS-001` | 服务热重启 | 等价迁移 | P1 | epoch 切换成功/超时/部分 Session 失败均有跨语言测试 |
| `OBS-001` | 指标和日志 | 兼容重构 | P1 | 关键计数可读取，关闭前最终 flush，有稳定测试 |
| `NFR-001` | Linux x86_64/arm64 | 平台目标 | P0 | 两平台 Debug/Release 构建与测试通过 |
| `NFR-002` | 内存与并发安全 | 质量属性 | P0 | ASan、UBSan、TSan 对规定测试集通过 |
| `NFR-003` | 性能不显著回退 | 质量属性 | P1 | 预先锁定场景下与 Go、UDS 比较，并满足确认后的阈值 |
| `NFR-004` | 可安装和可消费 | 发布能力 | P1 | CMake install/export、示例消费者和版本包 smoke 通过 |

## 6. 开发前必须确认的决策门

按标准流程一次确认一个重大问题，建议顺序如下：

1. 兼容目标：**已确认** Go/C++ 双向互通；它是开发和验收 oracle，不是部署依赖。
2. 平台节奏：默认 Linux x86_64 为首平台，arm64 在核心完成后加入同版本。
3. C++ 基线：**已确认 C++17**，与远程 GCC 8.5 对齐；未来升级 C++20 需独立决策和工具链方案。
4. 公共 API：确定异常、错误码或 `expected` 风格，以及 buffer view 的所有权表达。
5. 发布形态：静态/动态库、namespace、安装布局和是否接入 Conan/vcpkg。
6. 1.0 范围：确认热重启、异步 callback、SessionManager 是否必须随首版发布。
7. 性能阈值：在 Linux 基准机上定义消息大小、并发度、吞吐、P99 和内存目标。

决策结果进入 ADR；兼容目标已经关闭，剩余平台、语言/API 基线等决策在正式核心实现前确认。

## 7. 目标 C++ 架构

```text
include/shmipc/
  config.hpp              公共配置
  error.hpp               稳定错误模型
  buffer_view.hpp         零拷贝 view 与生命周期契约
  stream.hpp              Stream 公共 API
  session.hpp             Session 公共 API
  listener.hpp            服务端 API
  session_manager.hpp     客户端池 API

src/
  protocol/               大端控制协议、v2/v3 握手和事件状态机
  shm/                    mmap/memfd RAII、布局访问器、buffer pool、queue
  transport/              Unix/TCP、SCM_RIGHTS、epoll dispatcher
  core/                   Session、Stream、fallback、关闭状态机
  runtime/                executor/callback、timer、监控

tests/
  unit/                   编解码、布局、状态机、边界和不变量
  integration/            C++ 双进程测试
  interoperability/       Go↔C++ 双向测试与 oracle
  stress/                 多生产者、关闭竞争、资源耗尽
  data/golden/             固定 commit 生成的 byte/layout 基线

tools/
  go_oracle/              生成协议与布局 fixture 的小型 Go 工具
  layout_probe/           两端共享内存布局探针
  audit/                  需求/证据与发布检查
```

依赖方向必须保持：公共 API → core → protocol/shm/transport；平台系统调用只能位于 `transport/` 和 `shm/` 边界，协议和状态机不依赖 epoll 具体实现。

## 8. 设计不变量

1. 所有共享内存 offset/length 加法先检查溢出和边界。
2. 共享内存布局由显式 offset 访问器定义，不依赖 C++ struct padding。
3. 发布 queue tail 前，element 的三个字段已经对对端可见。
4. 清除 working flag 后必须重新检查队列，避免丢失并发唤醒。
5. 同一 Stream 进入 fallback 后不返回共享内存发送路径。
6. 零拷贝 view 的 owner 在 view release 前保持映射和 slice 存活。
7. Session 析构不允许遗留 dispatcher callback、Stream callback 或映射。
8. 控制 FD 上的帧写入必须保持串行和完整，不允许 header/body 与其他帧交错。
9. 协议错误关闭当前 Session，但不得越界访问、死循环或破坏其他 Session。
10. 优化不得改变与固定 Go 基线的可观察顺序和错误语义，除非 ADR 明确批准。

## 9. 里程碑与纵向切片

### M0：可构建、可测试、可审计骨架

切片：

- `S-0001`（已验收）：CMake 工程、library target、测试 target、编译告警和 `git diff --check` 门禁。
- `S-0002`（已验证）：Ubuntu 24.04 CI 覆盖 GCC/Clang × Debug/Release，并建立 ASan+UBSan/TSan 独立门禁；提交 `eeae84e` 的首轮六项矩阵全部通过。
- `S-0003`（已验证）：Go oracle 与固定 commit 校验，10 类事件 control-header golden，以及 C++ fixture 消费测试；run `32119710781` 云端通过。

退出条件：干净 Linux 环境能配置、编译、运行空测试和 oracle；CI 保存报告；项目 README 给出唯一命令入口。

状态：**已完成**。`S-0001..0003` 的本机、远端 Linux 和独立 GitHub Actions evidence 均已建立。

### M1：协议与共享布局锁定

切片：

- `S-0101`（已验证）：控制 header、事件枚举、metadata 和 fallback 编解码；run `32122127419` 七项作业全部成功。
- `S-0102`（已验证）：queue header/element 的 amd64 与 arm64 显式布局访问器。
- `S-0103`（已验证）：buffer manager/list/slice 显式布局；实验确认 creator counter `+20`、mapper counter `+24` 是角色隔离计数。
- `S-0104`（已验证）：损坏输入 corpus 覆盖截断、声明超长、size overflow、非法/未对齐 offset、循环链、tail/capacity/data-range 不一致。

状态：**已完成**。提交 `ed4c7a8` 的 GitHub Actions run `32125329954` 七项作业全部成功。

退出条件：所有 byte golden 与 Go 一致；布局不确定项有实验结论/ADR；fuzz smoke 与 sanitizer 不崩溃。

### M2：共享内存数据平面

切片：

- `S-0201`（已验证）：move-only RAII mmap/memfd/file mapping，显式区分 FD 借用/转移及文件创建者/mapper 清理责任。
- `S-0202`（已验证）：分级 buffer list 单进程分配回收；保留 sentinel、耗尽向大档位回退、角色 token/counter 和损坏 header 防护。
- `S-0203`（已验证）：lock-free seq_cst 双进程分配回收，以及 C++→Go、Go→C++ 链式 slice publish/adopt；run `32129419428` 七项作业全部成功。
- `S-0204`（已验证）：MPSC queue、working flag 和批量消费；保留 amd64 非自然对齐与 arm64 自然对齐布局；run `32131088262` 七项作业全部成功。
- `S-0205`（已验证）：BufferWriter/Reader、pin/release、跨 slice owned-copy 慢路径及 RAII 回收；run `32134325132` 七项作业全部成功。

状态：**已完成**。提交 `c1c23f9` 的 GitHub Actions run `32134325132` 通过 GCC/Clang Debug/Release、ASan+UBSan、TSan 与 Go oracle 七项门禁。

退出条件：`SHM-*`、`QUEUE-*` 的自动测试通过；压力测试计数守恒；ASan/UBSan/TSan 规定集合通过。

### M3：最小 v2 Go↔C++ 通信

切片：

- `S-0301`（已验证）：Unix/TCP exact blocking IO 与 Linux edge-triggered epoll dispatcher；提交 `17a668e` 的 run `32148166394` 七项门禁全部成功。
- `S-0302`（已验证）：v2 `/dev/shm` 握手；双向真实 Go Session 初始化、远端 50 轮重复及 run `32151993614` 七项门禁通过。
- `S-0303`（已验证）：单 Session/单 Stream C++ client↔Go server；20,000→17,000 字节、timeout、Polling 和双向 close 通过；提交 `050d7da` 的 run `32154121843` 七项门禁全部成功。
- `S-0304`（已验证）：Go client↔C++ server，动态绑定首个 Stream ID 2；三消息、跨 slice、Polling 和双向关闭通过；提交 `0347f34` 的 run `32158446306` 七项门禁全部成功。
- `S-0305`：多 Stream 并发、deadline、半关闭与错误传播。

退出条件：`COMP-001` 完成；两个方向的互操作报告绑定到准确 commit/build。

### M4：v3 memfd 与 fallback

切片：

- `S-0401`：版本协商状态机。
- `S-0402`：SCM_RIGHTS 发送/接收两个 FD 和异常路径。
- `S-0403`：共享内存耗尽、queue full、fallback 与 sticky ordering。
- `S-0404`：熔断/恢复期间拒绝新 Stream 的兼容行为。

退出条件：`COMP-002`、`STREAM-003` 完成；正常/异常握手和 fallback 双向互通。

### M5：完整公共 API 与管理能力

切片：

- `S-0501`：稳定 RAII Session/Stream API 和同步示例。
- `S-0502`：异步 callback executor、callback 内 Close 和销毁等待。
- `S-0503`：Listener 与兼容连接适配。
- `S-0504`：SessionManager、round-robin、Stream pool、断线重建。
- `S-0505`：指标、日志和 shutdown flush。

退出条件：API 示例与集成测试通过；所有权/线程安全/错误契约有文档；无已知资源泄漏。

### M6：热重启、arm64、性能与发布

切片：

- `S-0601`：epoch 热重启成功、超时和回滚路径。
- `S-0602`：Linux arm64 layout、构建和互操作矩阵。
- `S-0603`：64 B–4 MiB、多并发和 fallback 性能基线。
- `S-0604`：CMake install/export、示例消费者、版本元数据和包 smoke。
- `S-0605`：功能追踪、完整回归、安全/性能报告、tag 专属 CI 和 Release。

退出条件：当前范围需求证据 100%；Debug/Release、两架构、Sanitizer、互操作、性能和安装门禁通过。

## 10. 单切片闭环

每个 `S-*` 切片都执行：

1. 绑定需求 ID、用户结果、不包含项和失败模式。
2. 检查工作区与相关架构/回归文档。
3. 先增加稳定断言或 machine-auditable fixture，再实现成功和错误路径。
4. 运行相关测试、Debug 构建、`diff --check`；内存/并发/布局切片进入完整 Sanitizer 门禁。
5. 对互操作切片启动准确版本的 Go/C++ 对端，保存命令、日志摘要和 commit。
6. 稳定自动化测试可完整覆盖时，自测通过并记录 evidence 后直接创建本地候选提交。
7. 连续小切片优先积累为一批本地提交，在稳定检查点、云端证据需求或关键决策门前统一 push。
8. 只有需要用户参与设备、交互或主观判断等人工验收时才等待确认；`git push`、发布和 PR 等远程写操作仍需用户执行或逐次授权，远程 CI 通过后标记完成。

## 11. 测试策略

| 层级 | 重点 |
|---|---|
| 单元 | 大端编解码、offset 访问器、状态机、错误映射、边界与溢出 |
| Golden | Go oracle 输出的 header、metadata、queue header、buffer header 和事件序列 |
| 双进程集成 | C++↔C++ 的 mmap、queue、buffer、epoll 与关闭 |
| 跨语言互操作 | Go client↔C++ server、C++ client↔Go server；v2/v3 分开 |
| 并发/压力 | MPSC、consumer working 切换、Stream 并发关闭、callback 内关闭、资源耗尽 |
| 固定变异 smoke | 控制帧截断/位翻转、offset 越界、循环链、FD 数量错误 |
| Sanitizer | ASan、UBSan、TSan；核心布局/所有权/并发变更必须进入完整门禁 |
| 性能 | 64 B–4 MiB ping-pong、并发 Stream、batch 效率、fallback、峰值共享内存 |
| 安装 smoke | 干净消费者工程通过 `find_package` 构建、运行和链接 |

## 12. CI 规划

- M0 起先使用 [PROJECT_WORKFLOW.md](PROJECT_WORKFLOW.md) 中的远程 Linux 流程作为主要运行门禁；云 CI 建立后两者并行，远程主机不替代独立 CI。
- PR 快检：Linux x86_64 Debug、单元/相关互操作、格式、clang-tidy 或等价检查。
- 完整门禁：Debug + Release、ASan/UBSan、TSan 独立 job、Go 参考对端。
- 平台矩阵：x86_64 与 arm64；`fail-fast: false`。
- 候选/标签：全量互操作、压力、性能 smoke、安装包、验证报告和 SHA-256。
- 报告必须记录 C++ commit、Go submodule commit、OS/arch、编译器、构建配置和 artifact digest。

## 13. 风险清单

| ID | 风险 | 严重度 | 缓解/关闭条件 |
|---|---|---:|---|
| `R-001` | C++ 原子对象覆盖外部 mmap 存储的标准/ABI 语义 | 高 | 选定明确实现策略并在两编译器、两架构、TSan/压力下验证 |
| `R-002` | Go/C++ 内存序不匹配导致偶发丢消息 | 高 | 第一版 seq_cst；跨进程压力与计数守恒通过后再优化 |
| `R-003` | counter `+20/+24` 上游布局歧义 | 已关闭 | Go 双视图 probe 证明是角色隔离 counters；见 D-012 |
| `R-004` | 关闭与 callback 竞争造成 UAF/死锁 | 高 | 明确 owner/executor，状态机测试 + ASan/TSan |
| `R-005` | 恶意共享内存 offset 导致越界/循环 | 高 | checked arithmetic、访问上限、固定变异和 fuzz |
| `R-006` | fallback 与共享路径混用导致乱序 | 高 | per-Stream sticky 状态不变量和互操作顺序测试 |
| `R-007` | macOS 开发宿主掩盖 Linux 行为 | 中 | Linux 容器/VM/CI 从 M0 起作为正式证据环境 |
| `R-008` | 先复制 Go goroutine 结构导致 C++ 生命周期复杂 | 中 | 用显式 executor、RAII、stop/join 顺序重建语义 |
| `R-009` | 过早优化 lock-free/内存序 | 中 | 先兼容 golden 与压力基线，优化单独 ADR 和 benchmark |

## 14. 证据与追踪

每个需求只有一条规范记录；证据单独维护，至少包含：

```text
Evidence ID → Requirement IDs → Gate type → Result
            → C++ commit + Go commit → Platform/config
            → Log/report/artifact digest → Actor/time → Validity
```

计划中的首批 evidence：

- `E-BASE-001`：Go reference commit 与 submodule 状态。
- `E-BASE-002`：macOS arm64 `go test ./...` 的平台限制记录。
- `E-BASE-003`：Linux/amd64 `go test -c` 成功生成 ELF 测试二进制。
- `E-BASE-004`：交叉编译的固定 Go 基线在 `10.210.23.2` 完整测试 `PASS`、退出码 0。
- `E-M0-001`：C++17 骨架在 macOS/AppleClang 完成 Debug、CTest、install 与 ASan+UBSan；在远端 Linux/GCC 8.5 完成 Debug、CTest 与 `lib64` install。
- `E-M0-002`：用户安装 `libasan-8.5.0` 后，远端独立 ASan 构建和 CTest 通过；UBSan/TSan 仍因缺失对应 `/usr/lib64` runtime 而阻塞。
- `E-M0-003`：提交 `eeae84e` 的 GitHub Actions run [`32116398237`](https://github.com/supermanc88/shmipc-cpp/actions/runs/32116398237) 总结论 success；GCC/Clang × Debug/Release、ASan+UBSan、TSan 六项均执行并通过，常规四项安装验证通过。
- `E-M0-004`：固定 commit runner 与 overlay oracle 在本机通过；10 类 control-header golden（SHA-256 `ee6379a976c47c4d81c894ecf110132884ee8e48086091338cb17a8d8765fdfa`）被 Go `header.encode` 和 C++ test 共同验证，远端 GCC 8.5 Debug/ASan 两项 C++ 测试通过；提交 `34ef510` 的 GitHub Actions run [`32119710781`](https://github.com/supermanc88/shmipc-cpp/actions/runs/32119710781) 中 Go oracle 与完整七项矩阵全部成功。
- `E-M1-001`：生产 codec 覆盖 8 字节 header、事件 0..9、v2/v3 metadata 与 fallback；三份 golden 同时由固定 Go 编码器和 C++ round-trip 使用，并覆盖截断、非法字段、错误事件、尾随字节和帧上限。macOS AppleClang Debug/ASan+UBSan 与远端 Linux GCC 8.5 Debug/ASan 均通过；提交 `603933e` 的 GitHub Actions run [`32122127419`](https://github.com/supermanc88/shmipc-cpp/actions/runs/32122127419) 七项作业全部成功。
- `E-M1-002`：queue golden（SHA-256 `3c2dba47b214fe158582c7cb31ec9b74fa060819d848a87b253c1cf83d721697`）锁定 amd64 的 `cap/head/tail/working = 0/4/12/20` 与 arm64 的 `0/8/16/4`，element 均为 24 字节后连续三个 uint32。Go oracle 分别在 Darwin arm64 与 amd64 运行路径通过；C++ 双布局测试、远端 Linux GCC 8.5 Debug/ASan 及 run `32125329954` 通过。
- `E-M1-003`：buffer layout golden（SHA-256 `83a090638c0096c7619c66f22b6621ae6da6b77343150bacbfde4a99d6b6af5b`）锁定 manager 8 字节、list 36 字节、slice 20 字节。固定 Go 实验在同一内存上建立 creator/mapper 两视图，证明 pop/push 分别只修改 `+20/+24` 并独立归零；arm64 与 amd64 Go 路径、C++ accessors、远端 GCC 8.5 Debug/ASan 均通过。
- `E-M1-004`：固定损坏布局 corpus（SHA-256 `342f559d8106b538e8b41bc562041bb9dbaeb34b3e969275daccc1fc560149e5`）含 9 类失败模式；`validate_buffer_list_chain` 最多遍历 capacity 个节点，确定性分类截断、溢出、非法 offset、cycle、错误 tail、slice capacity 与 data range。本机 AppleClang Debug/ASan+UBSan 及远端 GCC 8.5 Debug/ASan 通过。
- `E-M1-005`：提交 `ed4c7a8` 的 GitHub Actions run `32125329954` 七项作业全部成功，M1 退出门禁通过。
- `E-M2-001`：move-only mapping owner 覆盖 file 双视图、创建端 unlink、memfd create/map 和 borrowed/transferred FD；本机 AppleClang Debug/ASan+UBSan 与远端 GCC 8.5 Debug/ASan 6/6 通过。
- `E-M2-002`：单进程分级 buffer pool 覆盖配置、初始化/映射、档位选择与回退、耗尽/完整回收、角色 counter/token 及损坏 head/tail/size/used-length；本机与远端 Debug/Sanitizer 7/7 通过。
- `E-M2-003`：32 位 always-lock-free seq_cst free-list 通过本机 20 轮、远端 10 轮父子进程压力及 AppleClang TSan；双向 Go oracle 以 20,000 字节链验证 C++ publish→Go adopt/recycle 和 Go publish→C++ adopt/recycle，最终 free-list 与角色净 counters 恢复。
- `E-M2-004`：提交 `281d024` 的 GitHub Actions run [`32129419428`](https://github.com/supermanc88/shmipc-cpp/actions/runs/32129419428) 中 GCC/Clang Debug/Release、ASan+UBSan、TSan 与 Go 双向 oracle 七项作业全部成功。
- `E-M2-005`：MPSC queue 以 4 producer/单 consumer 传递 20,000 elements，父子进程在 256 slots 上环绕 20,000 次，working 清零竞争重复 1,000 次；Go↔C++ 两方向各传 1,000 elements 并验证 working flag。本机 arm64 与 Rosetta x86_64 ASan+UBSan、本机 TSan、远端 GCC 8.5 Debug/ASan 与额外 20 轮压力通过。
- `E-M2-006`：提交 `4a0ef5c` 的 GitHub Actions run [`32131088262`](https://github.com/supermanc88/shmipc-cpp/actions/runs/32131088262) 中 GCC/Clang Debug/Release、ASan+UBSan、TSan 与 Go queue oracle 七项作业全部成功。
- `E-M2-007`：BufferWriter/Reader 覆盖 reserve/write/publish、单 slice borrowed view、跨 slice owned copy、peek/byte/string/discard、pin/release、析构回收与异常边界；20,000 字节 Go↔C++ helper 已改走生产 Buffer IO，并由 oracle 纠正为持续使用最大 tier 的上游分配语义。本机 Go oracle 10/10、ASan+UBSan 9/9、TSan 9/9，远端 GCC 8.5 Debug/ASan 各 9/9 通过；提交 `c1c23f9` 的 run [`32134325132`](https://github.com/supermanc88/shmipc-cpp/actions/runs/32134325132) 七项作业全部成功。
- `E-M3-001`：move-only control socket/listener 覆盖 adopted FD、partial exact IO、EOF/would-block、loopback TCP、pathname Unix socket、重复 bind、路径 unlink 与错误输入；本机 AppleClang Debug/ASan+UBSan/TSan、远端 GCC 8.5 Debug/ASan 各 10/10 通过。
- `E-M3-002`：Linux epoll dispatcher 覆盖 partial frame 保留、writev、EAGAIN/EPOLLOUT 背压、两个并发 writer 无帧交错、remote/local/shutdown close、buffer limit、callback 错误与 `on_data` 重入 close；远端 GCC 8.5 Debug/Release/ASan 各 11/11，Debug 专项连续 100 次通过。提交 `17a668e` 的 run [`32148166394`](https://github.com/supermanc88/shmipc-cpp/actions/runs/32148166394) 七项作业及关键步骤全部成功，`S-0301` 关闭。
- `E-M3-003`：v2 client 创建 buffer/双 queue 并发送单帧路径 metadata，server 无 ACK 地映射反向 queue 视图；C++ 覆盖错误版本/事件、截断、缺失路径、已有文件保护和失败回滚。远端固定 Go `newSession` 两方向互通并连续 50/50，GCC 8.5 Debug/ASan 各 12/12；提交 `3f2db07` 的 run [`32151993614`](https://github.com/supermanc88/shmipc-cpp/actions/runs/32151993614) 七项作业及关键步骤全部成功。
- `E-M3-004`：v2 C++ client 固定 Stream ID 1，以 BufferWriter publish→queue put→Polling 发送，以 queue pop→adopt→BufferReader 接收，并支持 timeout、queue/control close。C++ peer 与真实 Go server 均完成 20,000→17,000 字节跨 slice round-trip；远端 GCC 8.5 Debug/ASan 13/13、ASan Go 互操作及重复 50/50 通过；提交 `050d7da` 的 run [`32154121843`](https://github.com/supermanc88/shmipc-cpp/actions/runs/32154121843) 七项门禁全部成功，Go protocol oracle 14/14。
- `E-M3-005`：v2 C++ server 从首个 opened element 动态绑定真实 Go client Stream ID 2，一次 Polling 排空三条消息；两个独立互操作场景覆盖 C++/Go 主动 close。300 轮压力发现无 ACK 握手下 mapper 不能要求完整空闲链快照，修正为稳定布局与动态 offset 边界校验；本机 Debug/Release/ASan+UBSan/TSan 14/14，远端 Debug/ASan 14/14、普通互操作 300/300、ASan helper 50/50；提交 `0347f34` 的 run [`32158446306`](https://github.com/supermanc88/shmipc-cpp/actions/runs/32158446306) 七项门禁全部成功，Go protocol oracle 15/15。
- `E-LAYOUT-001`：M1 的 Go/C++ byte/layout golden。
- `E-INTEROP-*`：按 v2/v3、方向、架构分别记录互操作结果。

## 15. 下一步

1. 在 `S-0305` 扩展多 Stream、deadline/cancel 和完整错误传播。
2. 按固定 Go 实现的 client-originated 连续 Stream ID（2、3、4…）建立并发创建/关闭、半关闭和 Session 断开矩阵；不假定注释所称的奇偶分配或服务端主动开流。
3. 保留 live-pool mapping、两个方向单 Stream 与 300 轮压力作为持续回归。
