# 架构决策与不确定性

## 已验证结论

### D-001：参考基线固定为上游 main 的 `55c241e`

- 状态：已接受
- 证据：submodule gitlink 与 `third_party/shmipc-go` HEAD。
- 影响：所有兼容 fixture、测试和差异分析必须记录此 commit；升级上游需独立评审。

### D-002：首个执行平台应为 Linux x86_64

- 状态：已接受并具备远程执行环境
- 事实：`VerifyConfig` 仅接受 Linux amd64/arm64；事件层依赖 epoll；memfd/SCM_RIGHTS 为 Linux/Unix 路径。
- 基线验证：macOS arm64 的 `go test ./...` 因平台检查、并发 buffer 测试和 event dispatcher 失败；Linux/amd64 测试二进制可成功交叉编译，并已在 `10.210.23.2` 完整运行通过。
- 影响：M0 必须尽快建立 Linux 容器/VM/CI，macOS 仅作为编辑和交叉编译宿主。

### D-003：先实现协议兼容，再追求 C++ API 风格

- 状态：已接受；用户确认保留 Go↔C++ 双向互操作作为正确性验证手段
- 理由：共享内存布局、事件格式、原子可见性和资源生命周期决定 Go/C++ 能否互通；逐行翻译 Go API 不能证明兼容。
- 后果：先建立 wire/layout golden 与跨语言 oracle，再封装 RAII 和现代 C++ API。Go 仅是开发/验收 oracle，不是 C++ 产品的运行依赖。

### D-005：远程 Linux 主机作为 M0 起的主要运行验证环境

- 状态：已接受并验证
- 连接：本机 SSH 配置别名 `23.2`，解析为 `root@10.210.23.2`；直接使用 IP 不会命中该 Host 配置。
- 工作目录：`/home/chm/shmipc-cpp`，作为本地仓库的 source-only rsync 镜像。
- 环境：Kylin Linux Advanced Server V10、x86_64、kernel `4.19.90-20.0stable.x86_64`。
- 现有工具：CMake 3.20.6、GCC 8.5.0、Ninja 1.8.2、GNU Make 4.2.1。
- 缺失工具：Go、Clang、Docker、Podman；在明确工具链策略前不直接修改系统软件。
- 验证证据：Go 1.25.10 交叉编译的固定上游测试二进制在远端完整执行 `PASS`，退出码 0。
- 工作流：[docs/PROJECT_WORKFLOW.md](../docs/PROJECT_WORKFLOW.md)。

### D-006：最低语言标准采用 C++17

- 状态：已接受
- 理由：用户同意从远程现有工具链直接启动；GCC 8.5 对 C++17 的支持成熟，无需先改动目标机系统工具链。
- 影响：公共 API 和内部实现不得依赖 C++20；后续若升级最低标准，必须单独评审编译器矩阵、ABI 和消费端影响。
- 验证：最小 library/test/install 骨架已在 AppleClang 与远端 GCC 8.5 下完成配置、编译和测试。

### D-007：Linux CI 采用编译器/构建类型与 Sanitizer 分层矩阵

- 状态：已验证
- 常规矩阵：Ubuntu 24.04 上覆盖 GCC/Clang × Debug/Release，全部启用 warnings-as-errors，并执行 build、CTest 和 install。
- Sanitizer 矩阵：GCC Debug 分别执行 ASan+UBSan 与 TSan，避免互斥运行库组合并让失败归因清晰。
- 供应链约束：checkout 使用只读权限、`persist-credentials: false` 和递归 submodule；版本采用 2026-08-18 官方仓库当前示例的 `actions/checkout@v7`。
- 证据：`.github/workflows/ci.yml:1-89`；提交 `eeae84e` 的 GitHub Actions run [`32116398237`](https://github.com/supermanc88/shmipc-cpp/actions/runs/32116398237) 总结论为 success，六个矩阵作业及其 Configure/Build/Test 步骤全部成功，常规四项 Verify installation 成功。

### D-008：Go oracle 通过 overlay 访问固定上游内部编码器

- 状态：已验证
- 理由：`header.encode` 与事件常量未导出；复制算法不能构成独立 oracle，直接向 submodule 写测试文件又会污染固定参考实现。
- 决策：runner 先严格校验 submodule commit，再用 Go overlay 将外部 test source 映射为上游 package 的虚拟 `_test.go`，从而直接调用真实未导出符号且不修改 submodule。
- fixture：`tests/data/golden/control_headers.txt` 全量覆盖事件类型 0..9，并使用多字节 length 探针锁定大端布局；当前只证明 header primitive，不外推 event body 或状态机兼容。
- 证据：`tools/go_oracle/run_control_header_oracle.go:13-74`、`control_header_oracle_test.gotxt:165-345`、`tests/control_header_golden_test.cpp`；本机三项 CTest 和远端两项 C++ CTest/ASan 均通过，提交 `34ef510` 的 GitHub Actions run [`32119710781`](https://github.com/supermanc88/shmipc-cpp/actions/runs/32119710781) 中 Go oracle 作业及完整七项矩阵全部成功。

### D-009：自动化可完整验收的切片允许自测后直接本地提交

- 状态：已接受
- 授权：用户明确允许稳定自动化测试可验证的改动在自测通过后直接 commit，无需逐次确认。
- 边界：需要用户参与设备、交互或主观判断的测试仍须等待；`git push`、发布、PR 等远程写操作不包含在该授权内。
- 工作流：具体门禁见 `docs/PROJECT_WORKFLOW.md` 的“单切片执行闭环”。

### D-010：控制协议采用显式字段访问、结构化错误与有限帧长

- 状态：已验证；提交 `603933e` 的 GitHub Actions run `32122127419` 七项作业全部成功
- 决策：所有线上整数显式按大端读写，不用 packed struct 或 reinterpret；decoder 返回 `CodecError` 分类，并要求 metadata/fallback 输入精确等于一帧。
- 安全边界：默认最大帧长为 64 MiB，调用方可在 decode 时收紧；metadata 单个 path 受 uint16 上限约束。
- 兼容细节：metadata 按 queue path、buffer path 顺序；fallback 同时保留 32 位 raw status，并暴露 Go 接收路径使用的低 8 位 stream state。
- API 边界：当前头文件位于 `src/protocol/`，只作为内部接口，不随 install 导出；公共错误/API 模型另行决策。
- 证据：`src/protocol/control_codec.hpp:10-96`、`src/protocol/control_codec.cpp:110-275`、`tests/protocol_codec_test.cpp:46-199`。

### D-011：queue 布局按目标架构显式选择，普通访问不构造映射区对象

- 状态：已验证；提交 `ed4c7a8` 的 run `32125329954` 全部成功
- 布局：24 字节 header；amd64 为 `capacity/head/tail/working = 0/4/12/20`，arm64 为 `0/8/16/4`；12 字节 element 从 offset 24 起依次为三个 uint32。
- 决策：使用 `memcpy` 在映射字节和本机整数间转换，避免未对齐解引用、strict-aliasing 与 C++ struct padding；访问前校验 capacity、region size 和 slot。
- 边界：该文件只定义普通布局访问，不能把 helpers 当作原子操作；`S-0204` 已在独立的 `SharedQueue` 中实现跨进程原子和 MPSC 算法。
- arm64 约束：queue manager 总映射长度必须是 16 的倍数；与 Go 映射路径一致，当前会拒绝导致总长度不对齐的 capacity。
- 证据：`third_party/shmipc-go/queue.go:175-209`、`src/shm/queue_layout.cpp:81-192`、`tests/queue_layout_test.cpp:48-175`、`tools/go_oracle/control_header_oracle_test.gotxt:346-399`。

### D-012：buffer list 的 `+20/+24` 是角色本地净操作 counter，C++ 必须保留

- 状态：已验证；提交 `ed4c7a8` 的 run `32125329954` 全部成功
- 事实：`support arm64` 提交 `8ab38be` 将原来位于 `+20/+28` 的两个 uint64 push/pop counters 改为 int32 counter；创建视图绑定 `+20`，映射视图绑定 `+24`。
- 实验：同一内存建立 creator/mapper 两视图后，creator pop 只使 `+20` 变为 1，mapper pop 只使 `+24` 变为 1；双方各自 push 后各自 counter 归零。
- 修正：`S-0103` 时曾将字段解释为“各角色 outstanding 数量”；`S-0203` 的真实发送端 pop、接收端 push 链路证明该解释不完整。每个字段实际记录本角色本地 `pop - push` 净值，允许因接收对端 slice 而变负；等量双向链路后各自归零。
- 决策：C++ 以 `BufferListRole::creator/mapper` 显式选择 counter，不能合并字段或把 `+24` 当作 typo；关闭检查复现 Go 的“free size == capacity 且本角色净计数 == 0”。
- 布局：manager/list/slice header 分别为 8/36/20 字节；slice flags 只使用 offset 16 的低字节。
- 证据：`third_party/shmipc-go/buffer_manager.go:341-415,417-459,604-613`、commit `8ab38be` diff、`tools/go_oracle/control_header_oracle_test.gotxt:400-496`、`src/shm/buffer_layout.cpp:77-225`。

### D-013：损坏 free-list 使用有界遍历和确定错误分类

- 状态：已验证；提交 `ed4c7a8` 的 run `32125329954` 全部成功
- 决策：映射后的静态完整性检查最多访问 `capacity` 个节点；head/tail/next 必须在 buffer region 内且按 `20 + capacity_per_buffer` stride 对齐。
- 终止条件：无 next flag 的节点必须等于 header tail；遍历 capacity 个节点仍未终止则分类为 cycle。
- slice 条件：每个节点 capacity 必须等于 list capacity-per-buffer，且 `data_start + size` 用减法形式检查以避免溢出。
- 边界：validator 不提供并发快照语义，不应在对端正修改 free-list 时运行；并发算法另行实现和验证。
- 证据：`src/shm/buffer_layout.cpp:199-240`、`tests/data/corpus/layout_corruption.txt`、`tests/buffer_layout_test.cpp:185-289`。

### D-014：mapping 以 move-only owner 和显式资源责任建模

- 状态：已验证；提交 `281d024` 的 run `32129419428` 通过
- 决策：`SharedMemoryRegion` 唯一拥有 mapping，不可复制但可移动；析构/`reset` 负责 `munmap`，memfd 还负责关闭 descriptor。
- FD 语义：`borrowed` 先复制 descriptor，调用者保留原所有权；`transferred` 从函数入口接管，后续成功或失败都关闭。所有创建/复制 descriptor 使用 close-on-exec。
- 文件语义：创建使用 `O_EXCL`，避免静默截断同名共享区；创建者默认 unlink，mapper 永不隐式删除路径。相较 Go 按 map type 统一删除路径，这是不影响 wire/layout 的生命周期加固。
- 平台边界：file `MAP_SHARED` 路径支持当前 POSIX 开发/目标环境；memfd 创建仅在 Linux 编译，其他平台返回 `unsupported`。
- 证据：`src/shm/shared_memory_region.hpp:9-110`、`src/shm/shared_memory_region.cpp:20-315`、`tests/shared_memory_region_test.cpp:21-137`；本机 AppleClang Debug/ASan+UBSan 与远端 GCC 8.5 Debug/ASan 6/6 CTest 通过。

### D-015：单进程 buffer pool 保留 sentinel，并用角色 token 约束回收

- 状态：已验证；提交 `281d024` 的 run `32129419428` 通过
- 兼容语义：tiers 按 capacity 升序；请求从最小合适档位开始，耗尽后继续尝试更大档位；每个 free list 永远保留最后一个 sentinel，所以可分配数量为 `size - 1`。
- 所有权：`BufferAllocation` 不可复制且只可 move-construct，记录 memory、list 与 creator/mapper 角色；只有匹配 view 可回收，成功后 token 失效。token 析构不自动回收，以便后续跨进程转移逻辑所有权。
- 安全边界：mapper 要求 manager used-length 精确覆盖所有 lists，free-chain 节点数等于 header size，节点为 clean/free 状态；allocate/recycle 每次重新校验 head/tail 范围和 stride 对齐。
- 演进：该普通 read-modify-write 基线已由 `S-0203` 的 seq_cst 原子实现替代；sentinel、档位回退和 token 语义保持不变。
- 证据：`src/shm/buffer_pool.hpp:11-156`、`src/shm/buffer_pool.cpp:191-525`、`tests/buffer_pool_test.cpp:18-258`；本机 AppleClang Debug/ASan+UBSan 与远端 GCC 8.5 Debug/ASan 7/7 CTest 通过。

### D-016：共享 free-list 使用 always-lock-free 32 位 seq_cst 原子

- 状态：已验证；提交 `281d024` 的 run `32129419428` 通过
- 原子模型：size/head/tail/角色 counter 使用 GCC/Clang `__atomic` always-lock-free 32 位 primitive，统一 `__ATOMIC_SEQ_CST`，与 Go `sync/atomic` 默认顺序一致。
- 发布顺序：pop 先原子预留 size，再以 CAS 取得 head；push 先重置独占 slice，以 CAS 推进 tail并链接旧 tail，最后原子增加 size。旧 head/tail 可能在读取后被竞争者推进，因此只有 CAS 成功决定所有权，陈旧普通字段触发重试而非损坏判定。
- 对齐：tier capacity 必须为 4 的倍数；初始化和 mapper 都验证 size/head/tail 及 `+20/+24` counters 自然对齐。编译期拒绝非 lock-free 32 位目标。
- 证据：`src/shm/atomic_word.hpp:9-53`、`src/shm/buffer_pool.cpp:36-525`；本机 20 轮与远端 10 轮父子进程压力、AppleClang TSan/ASan+UBSan、远端 GCC 8.5 ASan 通过。

### D-017：跨进程 slice 链以 publish/adopt 转移逻辑所有权

- 状态：已验证；提交 `281d024` 的 run `32129419428` 通过
- 发送端：从最大档位向下分配，写入每个 slice 的 size、in-use/has-next 和下一个绝对共享内存 offset；发布成功后发送端 tokens 失效。
- 接收端：从 root offset 有界遍历并校验 slot、capacity、data range、in-use 和 cycle，再创建本角色 tokens；回收减少接收角色的净 counter。
- 验收：C++ 发布 20,000 字节链供 Go 读取/回收，Go 再发布等量链供 C++ 读取/回收；两方向 payload、root/next offsets、free-list 完整性和最终两个角色 counter 均验证。
- 证据：`src/shm/buffer_pool.cpp:282-525`、`tests/buffer_pool_interop_helper.cpp:17-75`、`tools/go_oracle/control_header_oracle_test.gotxt:18-113`。

### D-018：queue MPSC 使用本地 producer mutex 与共享 seq_cst 发布

- 状态：已验证；提交 `4a0ef5c` 的 run `32131088262` 通过
- 并发模型：与 Go 一致，每个方向由单个进程内的多个 producer 通过本地 mutex 串行写入，对端只有一个 consumer；不承诺多个进程同时生产同一方向。
- 发布顺序：producer 在 mutex 内检查 `tail-head < capacity`，写完 12 字节 element 后 seq_cst 增加 tail；consumer 读取 element 后 seq_cst 增加 head。`pop_batch` 只是在单 consumer 上循环，不改变共享格式。
- 架构：运行期只接受本机布局。arm64 使用自然对齐的 `+8/+16` head/tail；amd64 为 Go wire 兼容保留 `+4/+12` 非自然对齐 64 位原子，依赖目标 x86_64 的 always-lock-free builtin，并由远端 GCC 8.5 压力与 Rosetta x86_64 ASan+UBSan 验证。
- 唤醒：`mark_working` 仅在 `0→1` CAS 成功时要求发送 Polling；consumer 清零后复查队列，若竞争期间已有数据则恢复 1 并继续消费，避免丢失唤醒。
- 证据：`third_party/shmipc-go/queue.go:235-296`、`src/shm/shared_queue.cpp:74-173`、`tests/shared_queue_test.cpp:20-253`、`tools/go_oracle/control_header_oracle_test.gotxt:116-163`。

### D-019：Buffer IO 仅在单 slice 路径借用共享内存

- 状态：已验证；提交 `c1c23f9` 的 run `32134325132` 七项作业全部成功
- 读取边界：`read_bytes/peek` 完全落在当前 slice 时返回 borrowed view 并 pin；跨 slice 时复制为 owned view。`read_byte`、`read_string` 与 `discard` 只复制/推进，不产生新 pin。
- 生命周期：借用 view 在 `release_previous_read` 或 Reader 析构后失效；owned view 独立于 Reader。已耗尽且未 pinned 的 slice 自动回收，Writer/Reader 析构回收未发布、未读及 pinned tokens。
- 写入策略：大请求持续从最大档位分配，只有尾部再降到较小档位，复现 Go `allocShmBuffers`。Go oracle 在首版实现只分配一个最大 slice 后直接暴露该差异，修正后双向 20,000 字节链通过。
- 边界：本层只表示共享内存 `no_buffer`，不自行切换 socket；per-Stream sticky fallback 留给 Stream 集成，以维护共享路径与控制路径的顺序。
- 证据：`third_party/shmipc-go/buffer.go`、`src/shm/buffer_io.cpp:42-491`、`tests/buffer_io_test.cpp:34-228`、`tests/buffer_pool_interop_helper.cpp:18-85`；本机 Go oracle/ASan+UBSan/TSan 与远端 GCC 8.5 Debug/ASan 通过。

### D-004：v2 和 v3 是两个必须分别验收的握手路径

- 状态：已验证
- 事实：文件路径模式强制客户端走 v2 兼容初始化；memfd 模式走 v3 版本协商和 FD 传递。
- 影响：M1/M4 不得用单一握手测试代表两种模式。

## 设计风险与待验证事实

### R-001：共享内存使用本机字节序和手工 offset

- 证据：`queue.go`、`buffer_manager.go`、`buffer_slice.go` 通过 `unsafe.Pointer` 直接读写整数；只有控制协议使用 `binary.BigEndian`。
- 风险：C++ struct padding、endianness 或未对齐原子访问会造成静默破坏。
- 对策：C++ 不直接把共享区域 reinterpret 为普通 struct；集中定义 offset、显式 load/store，并用 `static_assert` 与 byte-level golden 验证。

### R-002：Go 原子操作到 C++ 内存序的映射需要按数据结构分别验证

- 事实：Go `sync/atomic` 默认顺序一致；C++ 若用 relaxed 可能破坏“先写 element、后发布 tail”的协议。
- 当前状态：buffer pool 的 32 位字段与 queue 的 64 位 head/tail 均使用 always-lock-free seq_cst primitive，并通过进程/线程压力、Sanitizer 和 Go↔C++ 链路验证；只有在互操作压力测试和基准证明后才考虑针对性放宽。
- 平台风险：amd64 queue 必须复现 Go 的 `+4/+12` 非自然对齐原子；远端 x86_64 已验证，下一批 GitHub Linux TSan 和未来 arm64 CI 仍作为持续门禁。

### R-003：`bufferList.counter` 的创建与映射偏移不一致

- 证据：`createFreeBufferList` 在 header `+20` 绑定 counter，`mappingFreeBufferList` 在 `+24` 绑定 counter；header 总长为 36。
- 原风险：创建端与映射端观察不同计数，可能影响 unmap 前的“buffer 全部归还”检查。
- 状态：已关闭并在 `S-0203` 修正语义。Go 双视图确认字段按角色选择；双向跨语言链路进一步证明它们是角色本地 `pop - push` 净计数，而非严格 outstanding 数量。
- 对策：C++ 保留 `+20/+24` 两字段并允许净值为负；完整双向链路后检查各角色归零，后续 Session 关闭测试继续验证非对称流量下的上游行为。

### R-004：上游测试在非 Linux 上不是可靠基线

- 事实：部分 Linux 文件没有文件名级平台排除，测试还直接依赖 OS/架构行为。
- 对策：所有正式基线、互操作和 sanitizer 证据来自 Linux；macOS 失败只记录为环境限制。

### R-005：异常输入边界检查需要加固

- 事实：控制 header 有 magic/version/type 检查，但共享内存 metadata 和部分链式 offset 读取依赖对端可信；`extractShmMetadata` 未完整验证 body 边界。
- 当前缓解：`S-0101` 已对控制帧 length、magic、version、event、body 截断、尾随字节、metadata 字段长度和 64 MiB 默认上限做显式检查。
- 当前缓解：`S-0104` 已增加 checked region size、有界链遍历和 9 类固定变异，覆盖截断、声明超长、size overflow、未对齐/越界 offset、cycle、tail/capacity/data-range 不一致。
- 剩余对策：真实 mmap 文件大小、跨多个 buffer list 的 manager 边界和运行期链并发仍在 M2 集成层继续验证。

### R-006：远端时钟漂移与部分 sanitizer 运行库缺失

- 事实：2026-08-18 本机时钟比远端快约 2 分 20 秒；保留时间戳的 rsync 会使 Ninja 持续判定 CMake 输入更新。用户随后安装 `libasan-8.5.0-10.el8.x86_64`，独立 ASan 构建和 CTest 已通过；`libubsan` 与 `libtsan` 仍未安装。
- 影响：标准同步必须使用 `--no-times --omit-dir-times`；远端 Debug 与 ASan 可作为当前门禁，UBSan/TSan 在运行库补齐前不能作为 Linux 通过证据。
- 对策：不擅自继续安装系统包；保留本机 AppleClang ASan+UBSan 快检，后续补齐远端 libubsan/libtsan 或引入独立工具链。

## 需要用户逐项确认的产品决策

1. 首版只支持 Linux x86_64，还是 x86_64 与 arm64 同期？计划默认先 x86_64、随后同里程碑补 arm64。
2. 首版是否包含 SessionManager、热重启、异步 callback 和 `net.Listener` 等价适配？计划将其放在核心互通之后，但仍纳入 1.0 范围。
3. 发布形态是源码库、静态库、动态库，还是同时提供？这会影响 ABI、安装和包管理策略。

## 修订历史

- 2026-08-18：基于上游 commit `55c241e` 建立初始架构分析、验证平台基线并识别布局风险。
- 2026-08-18：确认 Go↔C++ 双向互操作为开发验收目标；新增并验证远程 Linux x86_64 环境，固定 Go 基线完整测试通过。影响文档：`00_INDEX.md`、`01_OVERVIEW.md`、`02_DECISIONS.md`、`docs/SHMIPC_CPP_PORTING_PLAN.md`、`docs/PROJECT_WORKFLOW.md`。
- 2026-08-18：接受 C++17，建立 CMake/library/test/install 骨架；AppleClang 与远端 GCC 8.5 常规门禁通过，并记录远端时钟漂移与 sanitizer 运行库缺失。影响代码：`CMakeLists.txt`、`cmake/`、`include/`、`src/`、`tests/`；影响文档：本文件、`00_INDEX.md`、`01_OVERVIEW.md`、`dirs/root.md`、`graphs/relations.md` 及项目计划/工作流。
- 2026-08-18：用户安装远端 `libasan` 后，GCC 8.5 独立 ASan 构建与 `shmipc.version` 测试通过；修正原“远端 sanitizer 全部缺失”为“ASan 可用，UBSan/TSan 仍缺运行库”。证据：`CMakeLists.txt:16-18` 定义三个独立选项，`cmake/ShmipcProjectOptions.cmake:28-50` 组装 sanitizer flags；影响文档：本文件、`01_OVERVIEW.md`、`dirs/root.md`、`docs/PROJECT_WORKFLOW.md`、`docs/SHMIPC_CPP_PORTING_PLAN.md`。
- 2026-08-18：新增 Linux CI 分层矩阵；常规作业覆盖 GCC/Clang × Debug/Release，Sanitizer 作业覆盖 ASan+UBSan 与 TSan。证据：`.github/workflows/ci.yml:1-89`；影响文档：`00_INDEX.md`、`01_OVERVIEW.md`、`dirs/root.md`、`graphs/relations.md`、项目工作流与移植计划。
- 2026-08-18：提交 `eeae84e` 的首轮 GitHub Actions run `32116398237` 完整成功，验证六项 CI 矩阵均非跳过且步骤级通过；`S-0002` 从“待云端验证”转为“已验证”。影响文档：本文件、`01_OVERVIEW.md`、`dirs/root.md`、项目工作流与移植计划。
- 2026-08-18：新增固定 Go commit 检查、overlay oracle、10 类 control-header golden 和 C++ fixture 消费测试；本机 oracle/C++ 测试与远端 GCC 8.5/ASan 通过。证据：`tools/go_oracle/`、`tests/data/golden/control_headers.txt`、`tests/control_header_golden_test.cpp`；影响文档：索引、概要、根目录、关系图、项目计划/工作流及新增回归指南/ADR/功能矩阵。
- 2026-08-18：用户授权自动化可完整验收的切片在自测通过后直接创建本地 commit，同时保留人工测试与远程写操作的授权边界。影响文档：本文件、项目工作流和移植计划。
- 2026-08-18：提交 `34ef510` 的 GitHub Actions run `32119710781` 完整成功；Go 1.25.10 oracle 的 setup/configure/build/test 与其余六项矩阵全部通过，`S-0003` 和 M0 转为已验证。影响文档：索引、概要、本文件、根目录、oracle 目录、项目工作流、移植计划和功能矩阵。
- 2026-08-18：新增 `S-0101` 生产 control codec，覆盖 header、事件 0..9、v2/v3 metadata 与 fallback；固定 Go 编码器和 C++ round-trip 共用三份 golden，异常输入测试覆盖截断、非法字段、错误事件、尾随字节与帧上限。本机 AppleClang Debug/ASan+UBSan 及远端 GCC 8.5 Debug/ASan 通过。证据：`src/protocol/control_codec.*`、`tests/protocol_codec_test.cpp`、`tools/go_oracle/control_header_oracle_test.gotxt:165-345`；影响文档：索引、概要、本文件、root/protocol/oracle 目录、关系图、计划、工作流、回归指南和功能矩阵。
- 2026-08-18：提交 `603933e` 的 GitHub Actions run `32122127419` 七项作业全部成功，`S-0101` 转为已验证；新增 `S-0102` queue 显式布局访问器与双架构 golden，Go oracle 分别在 arm64/amd64 路径通过，C++ 本机与远端 GCC 8.5 Debug/ASan 通过。证据：`src/shm/queue_layout.*`、`tests/data/golden/queue_layout.txt`、`tests/queue_layout_test.cpp`；影响文档：索引、概要、本文件、root/shm/oracle 目录、关系图、计划、工作流、回归指南和功能矩阵。
- 2026-08-18：通过提交历史和 Go creator/mapper 双视图实验关闭 `bufferList.counter +20/+24` 偏移不确定项，当时将其解释为角色隔离 outstanding counters；新增 manager/list/slice C++ 显式布局访问器。该语义解释随后在 `S-0203` 修正。证据：上游 commit `8ab38be`、`tools/go_oracle/control_header_oracle_test.gotxt:400-496`、`src/shm/buffer_layout.*`、`tests/buffer_layout_test.cpp`。
- 2026-08-18：新增有界 buffer free-list validator 和 9 类固定损坏输入 corpus，确定性拒绝截断、溢出、非法 offset、循环、错误 tail、slice capacity 与 data range。证据：`src/shm/buffer_layout.cpp:199-240`、`tests/data/corpus/layout_corruption.txt`、`tests/buffer_layout_test.cpp:185-289`；影响文档：索引、本文件、shm 目录/文件、计划、回归指南和功能矩阵。
- 2026-08-18：提交 `ed4c7a8` 的 GitHub Actions run `32125329954` 七项作业全部成功，M1 完成；新增 `S-0201` move-only file/memfd mapping，明确 creator/mapper 路径清理和 borrowed/transferred FD 所有权。本机 AppleClang 与远端 GCC 8.5 Debug/Sanitizer 通过。证据：`src/shm/shared_memory_region.*`、`tests/shared_memory_region_test.cpp`；影响文档：索引、概要、本文件、root/shm 目录、mapping 文件、关系图、计划、回归指南和功能矩阵。
- 2026-08-18：新增 `S-0202` 单进程分级 buffer pool，保留上游 sentinel 与大档位回退语义，并以 move-only 角色 token、严格 free-chain/offset 校验加固回收边界。本机与远端 Debug/Sanitizer 通过。证据：`src/shm/buffer_pool.*`、`tests/buffer_pool_test.cpp`；影响文档：索引、概要、本文件、root/shm 目录、buffer pool 文件、关系图、计划、回归指南和功能矩阵。
- 2026-08-18：`S-0203` 将 free-list 更新升级为 always-lock-free 32 位 seq_cst 原子，实现 chain allocate/publish/adopt/recycle，并以双向 Go↔C++ 20,000 字节链路验证。并发/互操作实验修正先前 counter 推断：字段是各角色本地 pop-push 净值，不是严格 outstanding 数量。证据：`src/shm/atomic_word.hpp`、`src/shm/buffer_pool.*`、`tests/buffer_pool_test.cpp`、`tests/buffer_pool_interop_helper.cpp`、Go oracle；影响文档：索引、概要、本文件、root/shm/oracle 目录、buffer pool/atomic 文件、关系图、计划、工作流、回归指南和功能矩阵。
- 2026-08-18：提交 `281d024` 的 GitHub Actions run `32129419428` 七项作业全部成功，包含 Linux TSan、ASan+UBSan、GCC/Clang Debug/Release 与 Go 双向 chain oracle；`S-0201..0203` 云端门禁关闭。
- 2026-08-18：`S-0204` 新增 `SharedQueue`，按上游“本地 producer mutex + 共享 seq_cst head/tail + 单 consumer”模型实现 MPSC、batch 与 working flag；4 producer 并发、父子进程环绕、1,000 轮唤醒竞争、双向 Go oracle 和远端 amd64 20 轮压力通过。证据：`src/shm/shared_queue.*`、`tests/shared_queue*`、Go oracle；影响文档：索引、概要、本文件、root/shm/oracle 目录、shared queue/atomic 文件、关系图、计划、工作流、回归指南和功能矩阵。
- 2026-08-18：提交 `4a0ef5c` 的 GitHub Actions run `32131088262` 七项作业全部成功，包含 Linux TSan、ASan+UBSan 与 Go 双向 queue oracle；`S-0204` 云端门禁关闭。
- 2026-08-18：`S-0205` 新增 BufferWriter/Reader，确定单 slice 借用并 pin、跨 slice owned copy、byte/string/discard 不 pin，以及显式 release + RAII 回收语义；Go oracle 促使分档分配修正为持续最大档位。本机 oracle/ASan+UBSan/TSan 与远端 GCC 8.5 Debug/ASan 通过，等待云端门禁。影响文档：索引、概要、本文件、root/shm/oracle 目录、buffer IO 文件、关系图、计划、回归指南和功能矩阵。
- 2026-08-18：提交 `c1c23f9` 的 GitHub Actions run `32134325132` 七项作业全部成功，包含 GCC/Clang Debug/Release、Linux ASan+UBSan、TSan 与生产 BufferWriter/Reader 双向 Go oracle；`S-0205` 与 M2 退出门禁关闭。影响文档：索引、概要、本文件、root/shm/oracle 目录、buffer IO 文件、项目计划、工作流和功能矩阵。
