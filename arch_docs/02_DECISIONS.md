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

### D-020：握手阻塞 IO 与事件期 nonblocking transport 分层

- 状态：已验证；提交 `17a668e` 的 run `32148166394` 七项作业全部成功
- 阶段边界：复现 Go 的真实顺序——协议初始化先在 duplicated/owned FD 上 exact blocking read/write，握手完成后才设置 nonblocking 并注册 epoll。
- 所有权：`ControlSocket`/`ControlListener` 是 move-only owner；adopt 从调用入口消费 FD。所有 descriptors 设置 close-on-exec，并抑制 socket 写入导致的 SIGPIPE。
- IO 语义：exact helper 重试 EINTR、报告 partial progress，并区分 EOF、would-block 和其他系统错误；nonblocking 模式不在 helper 内隐式等待。
- Unix 路径：listener 不删除未知已有路径，只有成功 bind 后才拥有 unlink 责任，防止错误配置覆盖用户文件。
- 事件模型：Linux 使用 `EPOLLET` 并读到 `EAGAIN`；callback 消费缓冲前缀，未消费尾部跨事件保留并受上限保护。每连接整体串行 `write/writev`，EAGAIN 等待 EPOLLOUT generation，close 使所有等待者退出。
- 生命周期：FD syscall 与 close 互斥；连接终态只发布一次。callback 按连接串行，允许 `on_data` 内 close，但 `stop` 不允许由 worker callback 同步调用。callback 不保证固定线程亲和性。
- 证据：`third_party/shmipc-go/block_io.go:25-54`、`session.go:121-177`、`event_dispatcher_linux.go:247-263`、`src/transport/control_socket.cpp:18-405`、`src/transport/epoll_dispatcher.cpp:68-595`、`tests/control_socket_test.cpp:18-173`、`tests/epoll_dispatcher_test.cpp:1-351`；远端 GCC 8.5 Debug/Release/ASan 11/11，epoll 专项连续 100 次通过；run `32148166394` 的 GCC/Clang Debug/Release、ASan+UBSan、TSan 与 Go oracle 全部成功。

### D-004：v2 和 v3 是两个必须分别验收的握手路径

- 状态：已验证；v2 实现已完成本机/远端门禁，v3 尚待 M4
- 事实：文件路径模式强制客户端走 v2 兼容初始化；memfd 模式走 v3 版本协商和 FD 传递。
- 影响：M1/M4 不得用单一握手测试代表两种模式。

### D-021：v2 文件握手无 ACK，路径清理由 creator 独占

- 状态：已验证；提交 `3f2db07` 的 run `32151993614` 七项门禁全部成功
- 协议：文件模式 client 强制发送 version 2 `share_memory_by_file_path` metadata，server 读取并映射后直接完成初始化；不存在版本协商或 ACK。
- 顺序与方向：client 先创建 buffer、再创建 queue；server 按 metadata 先映射 queue、再映射 buffer。queue 前后两半在 server 视图中交换 receive/send 方向。
- ownership：C++ 只允许 creator 在最终析构或失败回滚时 unlink；mapper 不删除路径。该约束比上游双方可能清理同一路径更明确，且不改变 wire/layout 兼容性。
- transport：握手函数借用 blocking `ControlSocket`，不接管或切换 nonblocking；成功后 Session 才能将 socket 移入 dispatcher。deadline 留给 Session 统一取消。
- 证据：`src/core/v2_handshake.cpp`、`tests/v2_handshake_test.cpp`、`tests/v2_handshake_interop_helper.cpp` 与固定 Go overlay；远端双向互通 50/50，GCC 8.5 ASan 12/12。

### D-022：首个 Session 切片固定 Stream ID 1 和消息级接收

- 状态：已验证；提交 `050d7da` 的 run `32154121843` 七项门禁全部成功
- 范围：此早期 C++ v2 client 基线固定使用 ID 1（真实 Go server 接受任意未知非零 opened ID）；它不复现后来证实为从 2 开始的 Go client allocator。每个 queue element 对应一条完整 buffer chain 和一条接收消息。
- 唤醒：producer 只在 working `0→1` 时发 Polling；consumer drain 到 empty 后清零并复查，继承 `SharedQueue::mark_not_working` 的无丢唤醒不变量。
- close：正常 close 优先发送无 buffer 的 closed queue element，同时接受控制通道 StreamClose，以兼容 Go 在 queue full 时的关闭 fallback。
- 生命周期：Session 拥有 event connection 和 callback state；callback state 不反向强持有 connection，close 不形成环。receive timeout 只结束本次等待，不关闭 Session。
- 暂缓：fallback payload、queue-full retry、多 Stream 和公共 API 放在 `S-0305`/M4。
- 证据：`src/core/v2_client_session.cpp`、`tests/v2_client_session_test.cpp`、`TestV2ClientSessionInterop`；远端 Debug/ASan、ASan Go 互操作及 50/50 重复通过。

### D-023：v2 server 动态绑定首个 Stream，mapper 不快照验证活动 free-list

- 状态：已验证；提交 `0347f34` 的 run `32158446306` 七项门禁全部成功
- Stream：Go client 的 `nextStreamID` 初值为 1，首次 `OpenStream()` 原子递增后得到 ID 2；C++ server 在首个 opened queue element 到达时绑定非零 ID，后续单 Stream 切片拒绝不同 ID。多 ID 并发仍属于 `S-0305`。
- 消息与关闭：每个 queue element 保留消息边界；一次 Polling 排空连续消息。真实互操作分别验证 C++ 主动 close 与 Go 主动 close，避免半关闭时序让一侧 close 被合并。
- live mapping：v2 文件握手没有 ACK。Go 写完 metadata 后可立即分配 slice；mapper 建立时只校验稳定 manager/list 布局及动态 head/tail 的边界和 stride 对齐，不判定可瞬时为 0 的 size，也不遍历要求全空的 free-list 快照。allocate/adopt/recycle 继续严格验证所操作 slice/chain。
- 证据：`src/core/v2_server_session.hpp`、`src/core/v2_client_session.cpp`、`src/shm/buffer_pool.cpp`、`tests/v2_server_session_test.cpp` 与 `TestV2ServerSessionInterop`；远端 Debug/ASan 14/14、普通互操作 300/300、ASan helper 50/50；提交 `0347f34` 的 run `32158446306` 七项门禁与 Go oracle 15/15 通过。

### D-024：固定 Go 多 Stream 是 client-originated 连续 ID，不按奇偶分配

- 状态：源码已验证；指导 `S-0305`
- ID 事实：`Session` 注释称 client/server 分别使用奇/偶 ID，但 client 初值 1、server 初值 2，`OpenStream()` 实际执行 `atomic.AddUint32(..., 1)`，因此 client 依次得到 2、3、4…，server 若调用则依次得到 3、4、5…；实现没有保持奇偶。
- 方向事实：`getStream()` 仅在 `!isClient` 时为未知 opened ID 创建 Stream 并送入 `acceptCh`，源码同时标注 `todo support bidirectional streaming`。兼容切片只承诺 client Open→server Accept，不把 server 主动开流作为可互操作能力。
- 关闭与 deadline：remote close 把 opened 转为 half-closed，剩余数据读尽后返回 EOF，写入因 `IsOpen()==false` 被拒绝；从 half-closed 本地 Close 不再发送确认。read/write deadline 是 Stream 上持久绝对时间，其中 write deadline 只约束 queue-full 重试。
- 证据：`third_party/shmipc-go/session.go:35-39,145-152,249-290,560-583`、`stream.go:135-180,215-249,287-341,430-452`。

### D-025：多路实现分离连接级路由与 per-Stream 状态

- 状态：已验证；提交 `78913e6` 的 run `32204938990` 七项门禁全部成功
- 结构：保留已验证的单 Stream client/server 作为回归基线，新建 `V2MultiplexedSessionState` 统一拥有共享内存、路由表、accept 队列和 Session failure；每个 `V2StreamState` 独立拥有消息队列、mutex/condition variable 与关闭状态，`V2Stream` 只作为 move-only 句柄。
- Open/Accept：client 从 2 连续分配并先注册再发送；server 不因控制连接建立或 client 创建句柄而 Accept，只在未知 opened queue element 首次到达时创建并投递 Stream。固定 Go 不支持的 server-originated Open 不进入兼容承诺。
- close：opened 一侧主动 close 发送一次 closed element；收到 remote close 后进入 half-closed，本地 close 不回 ACK。测试必须指定唯一主动关闭方，不能把 close 当成请求/响应握手。
- ownership：Stream 强持有连接级 state 和 EventConnection，保证 Session owner 之外的句柄不会悬空；共享文件生命周期断言前必须释放全部 Stream 句柄。
- 验证：C++ 3 Stream 并发首包与双向消息，本地普通/ASan+UBSan/TSan 15/15、专项 100 次；远端 GCC 8.5 Debug/ASan 15/15、专项 100 次；真实 Go 双向 3 Stream 普通 100 轮、ASan 20 轮通过。
- 后续：persistent deadline、queue-full retry/close fallback、关闭后的路由表回收和 Session error 扇出已由 `S-0305b` 完成；数据 fallback 属于 M4。

### D-026：deadline 只约束等待，queue-full 重试与 close fallback 保持 Go 顺序

- 状态：已验证；提交 `78913e6` 的 run `32204938990` 七项门禁全部成功
- read deadline：Stream 保存可清除的 `steady_clock` 绝对时间；更新 deadline 会唤醒已经阻塞的 receive，并持续影响未来 receive。调用级 timeout 与 persistent deadline 取较早者。
- write deadline：普通 buffer publish/queue put 不因 write deadline 失败；只有 queue full 后的最多 10 次、每次 10ms retry 受它约束。失败路径回收已 publish chain。
- close fallback：closed element 若因 queue full 无法入队，编码 12 字节 v2 `StreamClose(stream_id)` 走有序控制连接；peer 同时接受 queue/control 两条关闭路径。
- 并发顺序：per-Stream send mutex 保证多个 send 有序；并发 close 先置 local closed 并唤醒 retry，再等待在途 send 结束后发送 close，禁止 close 越过数据。
- 生命周期：本地 close 后保留 state 供 remote close/Session failure 等待，句柄释放时从 route map 删除；remote close 保留 half-closed state 供消息排空。Session 断开先把原始 failure 扇出到所有 Stream，再清空 route/accept queue。
- 证据：满 8-slot queue 的立即 write timeout、25ms 后释放槽并重试成功、满队列控制 close、满队列 send 被并发 close 中断、阻塞 read deadline 更新及两个 Stream/Accept 同时被 Session close 唤醒；本地普通 100 轮与 ASan+UBSan/TSan，远端 Debug 专项 100 轮及 ASan 通过。

### D-027：v3 版本协商必须保留 client/server 首帧角色差异

- 状态：已验证；本机及远端门禁通过，等待云端提交验证
- wire：协商只使用 8 字节 header，type 为 `ExchangeProtoVersion(4)`；client 宣告最高版本 3，server 始终回复自身最高版本 3，最终版本由 client 取较小值。
- client：peer maximum 2 是合法降级，未来版本大于 3 时仍选择 3；低于 2 返回 unsupported。协商结果同时保留 peer/selected，后续初始化据此分流。
- server：固定 Go 在读取首帧后先按 header version 创建 initializer，因此只有 version 3 的 Exchange 帧能进入协商；version 2 Exchange 会被 v2 initializer 当作错误首事件，不能用对称的 `min()` 逻辑接受。
- 安全边界：上游 `blockReadEventHeader` 不核对 length，但本项目 `PROTO-001` 要求异常 length 安全拒绝，故双角色都要求 header-only frame 的 length 精确为 8。
- 模块边界：协商 API 借用 blocking socket，不创建/映射共享内存，不收发 FD；S-0402 依据 selected version 3 才进入 SCM_RIGHTS。
- 证据：固定 Go `protocol_manager.go:75-117,179-184`、`protocol_initializer.go:75-120`；C++ 单元异常矩阵，本机 Debug/ASan+UBSan/TSan 16/16、oracle 17/17；远端 GCC 8.5 Debug/ASan 16/16、双向 Go oracle 20 轮。

### D-028：v3 FD 传递采用单 NUL payload、固定顺序和严格接收 ownership

- 状态：已验证；本机/远端门禁与固定 Go 双向 oracle 通过，待云端门禁
- Go 兼容：`golang.org/x/sys/unix.Sendmsg` 在 stream socket 的空 payload 场景补入一个 NUL。C++ 发送端显式携带同一字节，接收端必须消费并验证它，防止 NUL 留在字节流中破坏下一 ACK header。
- 线序：单次 `SCM_RIGHTS` 固定发送 `[buffer_fd, queue_fd]`；C++ server 严格要求恰好两个 descriptor。数量不足或超出都视为握手失败，避免默默接受来源不明的额外能力。
- ownership：ancillary 接收结果是 move-only RAII owner；解析中已经取得的 descriptor 在未知 cmsg、超限、映射或后续初始化失败时自动关闭，只有显式 release 的两个 FD 才转移给 `SharedMemoryRegion`。
- ACK：server 先发送 `AckReadyRecvFD` 再接收 descriptors，并仅在 queue、buffer、pool 全部映射成功后发送 `AckShareMemory`。
- 证据：`third_party/shmipc-go/block_io.go`、`protocol_initializer.go`；`src/transport/control_socket.*`、`src/core/v3_handshake.*`、`tests/control_socket_test.cpp`、`tests/v3_handshake_test.cpp` 与 `TestV3HandshakeInterop`。本机 Debug/ASan+UBSan/TSan，远端 GCC 8.5 Debug/ASan、普通双向 100 轮和 ASan 20 轮通过。

### D-029：数据 fallback 只由 buffer 耗尽触发，并按 Stream 永久保持

- 状态：已验证；S-0403a 已由提交 `c8d6ade` 和 GitHub Actions run `32212075730` 七项门禁关闭。
- 触发：`BufferWriter::write_bytes` 返回 `no_buffer` 时回收已取得的 partial chain，将原 payload 编成 FallbackData。其他 buffer 错误仍保留细分错误，queue full 继续 10×10ms 重试并返回 queue/timeout，不触发 fallback。
- sticky：per-Stream `send_mutex` 串行化判定和写入；一旦本地 buffer 耗尽，后续发送不再尝试共享内存。C++ 接收方在应用取出 fallback 消息时标记；固定 Go 会把当前全部 pending slices 批量搬入 `recvBuf`，因此后续 fallback 已到达时可能在第一段 shared payload 返回前提前标记，oracle 只约束 fallback 之后必须为 true。
- 顺序：先前 shared message 的 Polling 和后续 FallbackData 共用有序控制连接，因此 peer callback 先 drain queue 再投递 fallback。每条 Stream 的消息队列保留消息边界。
- close 边界：fallback frame 在控制连接、close element 在共享队列，两者没有天然跨通道 barrier；运行时压力确认立即 close 可被较早 Polling 批量观察并越过仍在 socket 中的 fallback。数据保序 oracle 使用反向 fallback ACK 确认消费后再关闭，关闭兼容由独立矩阵验证；协议级 end-stream/barrier 留待后续切片明确。
- 证据：固定 Go `stream.go:205-271`、`protocol_manager.go:153-176`；`src/core/v2_multiplexed_session.cpp`、`tests/v2_multiplexed_session_test.cpp` 与 `TestV2FallbackInterop`。远端普通双向 50 轮、ASan helper 10 轮通过，临时 runtime-debugger 插桩已清理。

### D-030：v2/v3 复用同一多路数据面，以资源 variant 和运行期版本隔离握手差异

- 状态：已验证；提交 `4b2c7f1` 的 GitHub Actions run `32223456643` 七项门禁成功。
- 边界：握手仍由 `v2_client/server_handshake` 和 `v3_client/server_handshake` 分别负责；成功后统一构造 `V2MultiplexedSessionState`，以 `std::variant<V2SharedMemory, V3SharedMemory>` 持有资源，并通过窄转发函数访问 pool/send queue/receive queue。
- 协议：state 固定保存握手版本，callback 拒绝其他版本；Polling、FallbackData 和 StreamClose 均按该版本编码，防止 v3 资源复用时继续发 v2 header。
- API：为保持已有内部调用不变，v2 类型和启动函数原样保留；v3 暂以类型别名复用同一 move-only Stream/Session 句柄，并额外返回完整 `V3HandshakeStatus`。公共、版本中性的命名留到 M5 API 设计。
- 证据：`src/core/v2_multiplexed_session.cpp:97-165,332-363,878-940`、`src/core/v2_multiplexed_session.hpp:191-234`、`tests/v3_multiplexed_session_test.cpp`、`tests/v3_multiplexed_session_interop_helper.cpp` 与 `TestV3MultiplexedSessionInterop`。本机三套 18/18、远端 Debug/ASan 18/18、普通双向 50 轮及 ASan helper 10 轮通过；提交 `4b2c7f1` 的 run `32223456643` 七项成功。

### D-031：FallbackData 打开固定 30 秒 Session breaker，期间只拒绝新流

- 状态：已验证；提交 `39937bd` 的 GitHub Actions run `32329216783` 七项门禁成功，M4 关闭。
- 触发：成功解码收到的 FallbackData，以及发送端决定走 sticky/no-buffer fallback 时打开 breaker；queue-full StreamClose 不触发。
- 窗口：首次从 healthy 进入 unhealthy 后固定 30 秒，窗口内重复 fallback 不延长 deadline；到期由 steady clock 判定恢复，恢复后的下一次 fallback 可重新打开。
- 行为：`is_healthy()` 暴露状态；client `open_stream()` 在分配 ID 前返回 `V2SessionError::unhealthy`。已有 Stream 的 send/receive/close 不受 breaker 阻断。
- 并发：deadline 使用 seq_cst atomic ticks 和 CAS，不创建 timer 线程，不与 Session/Stream mutex 形成新的锁序。
- 证据：固定 Go `session.go:230-268,546-558`、`stream.go:256-270`、`protocol_manager.go:153-176`；C++ `src/core/v2_multiplexed_session.hpp:16-34,112-115`、`.cpp:16-38,148-159,513-534,727-747`。本机 Debug/ASan+UBSan/TSan、远端 Debug/ASan 各 18/18，v2/v3 固定 Go 双向普通 50 轮及 ASan helper 10 轮通过。

### D-032：公共 client API 使用版本无关 move-only PImpl 与共享 EventLoop owner

- 状态：已验证实现，待云端门禁；`S-0501` 本机与远端 Linux 门禁通过。
- API：安装头只导出 `ClientConfig`、`Status/Error`、move-only `Session/Stream` 及 `connect_tcp/connect_unix`；协议版本、handshake、dispatcher、queue/pool 错误不进入 public type layout。
- ownership：`Session::Impl` 持有内部 Session 与共享 EventLoop；client 当前是其 event loop 唯一 Session owner，Listener 场景可由多个 accepted Session 共享。显式 `close()` 先停止可选 telemetry worker，再关闭 Session 并释放 EventLoop owner；最后一个 owner 析构时 stop/join。`Stream` 独立持有内部句柄。
- 模式：file 模式可使用 TCP/Unix；memfd 需要 Unix socket 传递 descriptor，TCP+memfd 在建立连接前返回 `unsupported`。公共默认 queue/pool/tier 与固定 Go 默认值一致。
- 错误：内部 transport/handshake/codec/queue/pool 细节归一化为稳定 public 分类，同时保留 `system_error`；不会把内部枚举固定进安装 ABI。
- 并发：不同 Stream 可并发，同一 Stream 的 mutation 依赖已验证内部串行化；同步 API 只返回 owned `std::vector<uint8_t>`，因此本切片没有暴露 borrowed-view 生命周期。
- 构建：库公开传播 `Threads::Threads`，安装 config 用 `find_dependency(Threads)` 恢复依赖；CI 在 install 后以独立工程 `find_package(shmipc CONFIG REQUIRED)` 编译运行消费者。
- 证据：`include/shmipc/session.hpp:13-170`、`src/session.cpp:13-369`、`tests/public_session_test.cpp:45-185`、`tests/package_consumer/`、`examples/synchronous_client.cpp`、`CMakeLists.txt:12-81`。本机 Debug/ASan+UBSan/TSan 与远端 GCC 8.5 Debug/ASan 各 19/19，两个平台安装消费 smoke 通过。

### D-033：异步 callback 使用共享 executor 与每 Stream 串行 pump

- 状态：已验证实现，待云端门禁；`S-0502` 本机与远端 Linux 门禁通过。
- 分层：核心 `V2Stream` 只发布带 token 的非阻塞 readable notifier，复制后在 Stream mutex 外调用；公共 `AsyncCallbackState` 负责 generation/scheduled pump，用户 callback 不运行于 event-loop。
- 并发：每个 Stream 至多一个 scheduled pump，保证 callback 串行；不同 Stream 共享固定线程池并可并行，不创建 per-Stream 阻塞线程。
- ownership：subscription/state 强持有 callback、executor 与 Stream PImpl；核心 notifier 只 weak 捕获 state，PImpl 只 weak 指向 callback control，不形成环。
- 关闭：普通线程 close/stop 等待在途 callback 完成；executor callback 内 close/stop 不自等待，pump 在 callback 返回后发布唯一终止 callback。`on_data` 异常被隔离为 `callback_error` 并触发本地关闭。
- 证据：`include/shmipc/session.hpp:105-199`、`src/callback.cpp:56-443`、`src/public/session_impl.hpp:10-27`、`src/core/v2_multiplexed_session.cpp:270-344,675-740`、`tests/public_session_test.cpp`。专项连续 20 轮，本机 Debug/ASan+UBSan/TSan、远端 GCC 8.5 Debug/ASan 各 19/19，macOS/Linux 安装消费者通过。

### D-034：Listener 与 accepted Session 共享 EventLoop，兼容层保留字节流后缀

- 状态：已验证实现，待云端门禁；`S-0503` 本机与远端 Linux 门禁通过。
- ownership：Listener 和每个 accepted Session 强持有同一个 EventLoop；关闭 Listener 只关闭 listening FD，最后一个 owner 析构才 stop/join dispatcher。
- accept：listening FD 为 nonblocking；单 accept mutex 串行化握手，poll 最长 50 ms 后复查关闭状态。timeout 只覆盖等待控制连接，握手在连接建立后同步完成。
- 角色：client Session 只 open，server Session 只 accept；错误角色返回稳定 `unsupported`。
- 协议边界：公开握手上限在 v2/v3 入口分别 clamp 到 metadata 硬上限。远端真实 Linux 首次验证发现未 clamp 会让默认 64 MiB 配置在握手读取前失败，现由默认配置回归锁定。
- 兼容层：StreamConnection 一次 write 发布一条消息；read 保存 pending suffix、拼接立即就绪的后续消息，并在部分数据后延迟报告非 timeout 终止状态。
- 证据：`include/shmipc/listener.hpp:12-65`、`src/listener.cpp:58-259`、`src/public/session_impl.hpp:44-75`、`src/stream_connection.cpp:14-140`、`tests/public_listener_test.cpp:57-223`。远端专项连续 20 轮，GCC 8.5 Debug/ASan 与本机三套配置各 20/20，安装消费者通过。

### D-035：SessionManager 使用 RAII lease、per-Session FIFO pool 与 generation 重连

- 状态：已验证实现，待云端门禁；`S-0504` 本机与远端 Linux 完整门禁通过。
- 选路：默认保持固定 Go 基线的 32 次批量 round-robin，公式为 `(selection / batch) % session_count` 且 selection 从 1 开始；batch 可配置但不能为 0。
- ownership：`PooledStream` 是独占 move-only lease，析构自动尝试回池；显式 `close` 放弃复用。lease 弱引用池，因此不会延长 Manager 生命周期。
- 复用：每 Session 使用有界 FIFO。fallback、未读消息、活动 callback、关闭/失败状态、旧 generation、unhealthy Session 和超容量 lease 均关闭；return 与 checkout 两次验证覆盖空闲期状态变化，成功复用时清除 persistent deadline。
- 重连：每池独立 worker 轮询控制连接；断线后 generation 加一并关闭旧 Session/idle Streams，按配置间隔重连。file 名称追加 PID/Session/generation，避免旧 checked-out lease 持有的 mapping 与新路径冲突。
- 并发：固定 pool vector 初始化后不再清空；atomic stopping/selection 与 per-pool mutex 使 `close` 可与 `get_stream` 并发。shutdown 先通知/join workers，再关闭池资源。
- 边界：本切片不实现 Go hot-restart epoch；连接建立后的 handshake 仍沿用同步 Session API，没有独立 handshake timeout。
- 证据：固定 Go `session_manager.go`；`include/shmipc/session_manager.hpp:13-104`、`src/session_manager.cpp:20-378`、`src/core/v2_multiplexed_session.cpp:505-514,764-788`、`tests/public_session_manager_test.cpp`。本机四套与远端 Debug/ASan 各 21/21，远端专项连续 20 轮，macOS/Linux 安装消费者通过。

### D-036：可观测性使用累计快照、per-Session worker 与非侵入式错误隔离

- 状态：已验证实现，待云端门禁；`S-0505` 本机与远端 Linux 门禁通过。
- API：公共 `SessionMetrics` 聚合 Session ID/角色/协议版本、性能累计计数、稳定性累计计数与 queue/Stream/shared-memory gauge；`Session::metrics()` 可同步读取。Client/Listener 配置可注入共享 Monitor/Logger、周期和日志阈值，默认不隐式输出。
- 采集：核心在 Polling、数据收发、共享内存分配失败、fallback、queue full 和非本地 control close 发生点更新 relaxed 原子；采样不承担数据发布同步。共享内存 capacity/used 按固定 Go `capacity` 与原子 `size` 口径计算，capacity 包含 sentinel。
- 生命周期：每个受监控 Session 启动一个 worker；周期回调与 shutdown 最终回调都读取累计快照。关闭顺序固定为 stop/join worker（最终 snapshot→flush）→核心 Session close→mapping/EventLoop 释放，因此最终采样不会访问已 unmap 内存。
- 隔离：Monitor/Logger 可被多个 Session worker 并发调用，调用方实现必须线程安全；emit/flush/Logger 的异常或失败被捕获并记录，不把成功的数据面关闭改为失败。该处理与固定 Go 忽略 `Monitor.Flush()` 返回错误的行为一致。
- 重入边界：Monitor callback 不得同步关闭或销毁当前上报 Session，因为 shutdown 需要 join 同一 worker；该限制写入公共头。Monitor 接口不暴露 Session 引用，降低误用概率。
- 重连：SessionManager 的每次成功连接拥有独立 Session ID 和指标生命周期；manager 另外通过 Logger 记录重连失败/成功。hot-restart 成功/失败计数留给 `S-0601`，不在此切片伪造。
- 证据：固定 Go `stats.go`、`session.go:467-482,715-755`；C++ `include/shmipc/session.hpp:52-139`、`src/session.cpp:207-348`、`src/core/v2_multiplexed_session.cpp:150-188,366-419`、`src/shm/buffer_pool.cpp:165-201`、`tests/public_observability_test.cpp:44-277` 与底层指标断言。本机 Debug/Release/ASan+UBSan/TSan、远端 GCC 8.5 Debug/ASan 各 22/22，远端专项连续 20 轮及 macOS/Linux 安装消费者通过。

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

- 2026-08-20：`S-0505` 新增累计 Session 指标、同步 snapshot、线程安全 Monitor/Logger 注入、per-Session 周期 worker、关闭前最终 snapshot/flush 和重连日志。对照固定 Go 修正两点语义：flush 失败不改变 transport close 结果，共享内存 capacity 包含 sentinel 且 used 从原子 free size 计算。证据：`include/shmipc/session.hpp`、`src/{session,listener,session_manager}.cpp`、`src/core/v2_multiplexed_session.cpp`、`src/shm/buffer_pool.cpp`、`tests/public_observability_test.cpp`；影响文档：索引、概要、本文件、root/core/shm 目录、公共/PImpl/多路/buffer pool 文件、关系图、README、计划、回归指南和功能矩阵。
- 2026-08-20：`S-0504` 新增 SessionManager、批量 round-robin、per-Session FIFO Stream pool、RAII lease、generation 隔离与独立重连 worker。Linux 运行时诊断修正默认空 Result 测试误判，并发现/修复丢弃不可复用 idle Stream 后未新建的问题；临时插桩已清理。证据：`include/shmipc/session_manager.hpp`、`src/session_manager.cpp`、`tests/public_session_manager_test.cpp`；影响文档：索引、概要、本文件、root/public/manager 文件、关系图、计划、回归指南和功能矩阵。
- 2026-08-20：`S-0503` 新增 move-only Listener、server Session/AcceptStream、共享 EventLoop 与 StreamConnection。远端 Linux 运行时调试修正公开握手上限未经协议收窄导致 v2/v3 默认配置失败；临时插桩已清理。影响文档：索引、概要、本文件、root/public 文件、关系图、ADR、计划、回归指南和功能矩阵。
- 2026-08-20：`S-0502` 新增共享 callback executor、核心 tokenized readable notifier、每流 generation pump、RAII subscription、callback 内 Close/异常隔离和普通线程销毁等待。证据：`include/shmipc/session.hpp:105-199`、`src/callback.cpp:56-443`、`src/public/session_impl.hpp:10-27`、`src/core/v2_multiplexed_session.cpp:270-344,675-740`；影响文档：索引、概要、本文件、root/core 目录、公共/异步/PImpl/多路文件、关系图、ADR、计划、回归指南和功能矩阵。
- 2026-08-20：`S-0501` 新增版本无关 move-only Session/Stream 公共 API、PImpl client 适配、同步示例、线程 package 依赖和安装后外部消费者；Linux 测试覆盖 v2 TCP/file 与 v3 Unix/memfd。影响文档：索引、概要、本文件、root/core 目录、公共 API/实现文件、关系图、README、计划、回归指南和功能矩阵。
- 2026-08-20：提交 `39937bd` 的 GitHub Actions run `32329216783` 七项门禁成功，`S-0404` 与 M4 正式关闭。
- 2026-08-19：`S-0404` 新增 30 秒 Session circuit breaker、`is_healthy()` 与 `unhealthy` 开流错误；v2/v3 固定 Go 双向验证发送端和接收端均拒绝新流且已有 Stream 可继续。压力测试修正了“Go 必须在首个 shared read 后仍非 fallback”的时序假设：`pendingData.moveToWithoutLock` 会批量搬运后续 fallback。影响文档：索引、概要、本文件、core/oracle 目录、多路 Session 文件、关系图、计划、工作流、回归指南和功能矩阵。
- 2026-08-19：`S-0403b` 将多路 Session state 通用化为 v2 文件/v3 memfd 资源 variant，新增运行期 header version、v3 启动 API、C++ 端到端测试与固定 Go 双向 Session oracle。证据：`src/core/v2_multiplexed_session.*`、`tests/v3_multiplexed_session*`、`tools/go_oracle/control_header_oracle_test.gotxt:181-333`；影响文档：索引、概要、本文件、core/oracle 目录、Session 文件、关系图、计划、回归指南和功能矩阵。

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
- 2026-08-18：M3 `S-0301` 首个子切片新增 move-only Unix/TCP control socket/listener、exact blocking IO、partial/EOF/would-block 分类及 Unix 路径所有权；本机三套配置与远端 GCC 8.5 Debug/ASan 通过。证据：`src/transport/control_socket.*`、`tests/control_socket_test.cpp`；影响文档：索引、概要、本文件、root/transport 目录、control socket 文件、关系图、计划、回归指南和功能矩阵。
- 2026-08-18：`S-0301` 新增 Linux edge-triggered epoll dispatcher、消费式读缓冲、串行并发写、EPOLLOUT 背压、eventfd 停止及唯一关闭通知；远端 GCC 8.5 Debug/ASan 11/11、专项 100 次通过，等待云端 Linux TSan。证据：`src/transport/epoll_dispatcher.*`、`tests/epoll_dispatcher_test.cpp`；影响文档：索引、概要、本文件、root/transport 目录、epoll 文件、关系图、计划、回归指南和功能矩阵。
- 2026-08-18：提交 `be2a5b6` 的 run `32139049571` 中六项门禁成功，GCC 13 Release 因 fortify 要求处理 eventfd syscall 返回值而编译失败；新增 `consume_wake/signal_wake` 显式处理返回值并重试 `EINTR`，本机 Release/TSan 与远端 GCC 8.5 Debug/Release/ASan 复验通过，等待修复提交的云端门禁。证据：`src/transport/epoll_dispatcher.cpp:68-82`；影响文档：本文件、root/transport 目录和 epoll 文件。
- 2026-08-18：提交 `17a668e` 的 GitHub Actions run `32148166394` 七项作业及关键步骤全部成功，包含 GCC 13 Release、Linux ASan+UBSan、TSan 与 Go oracle；`S-0301` 正式关闭，M3 转入 `S-0302` v2 `/dev/shm` 握手。影响文档：索引、概要、本文件、root/transport 目录、epoll 文件、移植计划和功能矩阵。
- 2026-08-18：`S-0302` 新增 v2 文件路径握手、move-only 共享资源聚合及细分错误模型；固定 Go overlay 在远端 Linux 完成两个方向真实 Session 初始化并连续 50/50，GCC 8.5 Debug/ASan 通过，等待 GitHub Actions 门禁。影响文档：索引、概要、本文件、core/oracle 目录、关系图、移植计划、工作流、回归指南和功能矩阵。
- 2026-08-18：提交 `3f2db07` 的 GitHub Actions run `32151993614` 七项作业及关键步骤全部成功，包含 GCC/Clang Debug/Release、ASan+UBSan、TSan 与 Linux Go↔C++ v2 handshake oracle；`S-0302` 正式关闭。影响文档：索引、概要、本文件、root/core 目录、项目工作流、移植计划和功能矩阵。
- 2026-08-18：`S-0303` 新增 v2 client 单 Session/Stream，将握手资源接入 epoll，以 buffer chain、queue 和 Polling 完成消息收发及关闭；真实 Go server 互操作和远端 Debug/ASan/50 轮压力通过。提交 `050d7da` 的 GitHub Actions run `32154121843` 七项作业及关键步骤全部成功，Go protocol oracle 为 14/14，`S-0303` 正式关闭。影响文档：索引、概要、本文件、root/core/oracle 目录、关系图、项目工作流、移植计划、回归指南和功能矩阵。
- 2026-08-18：`S-0304` 新增 v2 server 单 Stream，按真实 Go client 首个 ID 2 动态绑定，完成多消息、跨 slice 与两个方向的关闭互操作。300 轮压力发现并修正无 ACK 握手下 mapper 对活动 free-list 的错误快照假设；本机三套 Sanitizer、远端 Debug/ASan、普通 300/300 与 ASan 50/50 通过，等待云端门禁。影响文档：索引、概要、本文件、root/core/shm/oracle 目录、buffer pool/server session 文件、关系图、移植计划、回归指南和功能矩阵。
- 2026-08-19：提交 `0347f34` 的 GitHub Actions run `32158446306` 七项作业及关键步骤全部成功，GCC/Clang Debug/Release 的安装验证实际执行，Go protocol oracle 为 15/15；`S-0304` 正式关闭。影响文档：索引、概要、本文件、root/core/oracle 目录、server session 文件、项目工作流、移植计划和功能矩阵。
- 2026-08-19：`S-0305` 源码追踪证伪“奇偶 Stream ID”注释假设；固定 Go 实现按 1 递增且只支持 client-originated Open→server Accept。计划改为连续 ID 与单向开流兼容矩阵。影响文档：本文件、移植计划。
- 2026-08-19：提交 `78913e6` 的 GitHub Actions run `32204938990` 中 GCC/Clang Debug/Release、ASan+UBSan、TSan 与 Go protocol oracle 七项作业全部成功；oracle CTest 16/16，`S-0305a/b` 与 M3 正式关闭。影响文档：索引、本文件、core/oracle 目录、项目计划、工作流和功能矩阵。
- 2026-08-19：`S-0401` 新增独立 v3 版本协商状态机，固化 client 可降级、server 仅接受 v3 首帧的非对称语义；本机三套 sanitizer、固定 Go oracle 17/17、远端 GCC 8.5 Debug/ASan 16/16及双向互操作 20 轮通过，等待云端门禁。影响文档：索引、本文件、core/oracle 目录、版本协商文件、项目计划和回归指南。
- 2026-08-19：提交 `807b4fa` 的 GitHub Actions run `32207020590` 七项作业全部成功，`S-0401` 云端门禁关闭。
- 2026-08-19：`S-0402` 新增 Linux SCM_RIGHTS RAII descriptor 传递与完整 v3 memfd 资源握手，固定单 NUL payload、`[buffer, queue]` 线序、严格两个 FD 和 ACK 时序；本机三套配置、远端 Debug/ASan、固定 Go `newSession` 双向普通 100 轮和 ASan 20 轮通过，等待云端门禁。影响文档：索引、概要、本文件、core/transport/oracle 目录、v3/control socket 文件、移植计划和回归指南。
- 2026-08-19：提交 `568817c` 的 GitHub Actions run `32209295664` 七项作业全部成功，`S-0402` 云端门禁关闭。
- 2026-08-19：`S-0403a` 新增 buffer 耗尽触发的 per-Stream sticky FallbackData，接收消费后同步切换，queue full 保持原重试语义。跨语言压力确认 fallback/control 与 queue close 没有天然 barrier，oracle 改用反向 fallback ACK 分隔数据消费和关闭；普通双向 50 轮、ASan helper 10 轮及提交 `c8d6ade` 的 run `32212075730` 七项门禁通过。影响文档：索引、概要、本文件、core/oracle 目录、多路 Session 文件、关系图、计划和回归指南。
