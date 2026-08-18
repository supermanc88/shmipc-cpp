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

### D-004：v2 和 v3 是两个必须分别验收的握手路径

- 状态：已验证
- 事实：文件路径模式强制客户端走 v2 兼容初始化；memfd 模式走 v3 版本协商和 FD 传递。
- 影响：M1/M4 不得用单一握手测试代表两种模式。

## 设计风险与待验证事实

### R-001：共享内存使用本机字节序和手工 offset

- 证据：`queue.go`、`buffer_manager.go`、`buffer_slice.go` 通过 `unsafe.Pointer` 直接读写整数；只有控制协议使用 `binary.BigEndian`。
- 风险：C++ struct padding、endianness 或未对齐原子访问会造成静默破坏。
- 对策：C++ 不直接把共享区域 reinterpret 为普通 struct；集中定义 offset、显式 load/store，并用 `static_assert` 与 byte-level golden 验证。

### R-002：Go 原子操作到 C++ 内存序的映射尚未被正式规范化

- 事实：Go `sync/atomic` 默认顺序一致；C++ 若用 relaxed 可能破坏“先写 element、后发布 tail”的协议。
- 决策建议：第一版统一使用 `std::memory_order_seq_cst`；只有在互操作压力测试和基准证明后才针对性放宽。

### R-003：`bufferList.counter` 的创建与映射偏移不一致

- 证据：`createFreeBufferList` 在 header `+20` 绑定 counter，`mappingFreeBufferList` 在 `+24` 绑定 counter；header 总长为 36。
- 风险：创建端与映射端观察到不同计数，影响 unmap 前的“buffer 全部归还”检查。
- 状态：代码事实已确认，行为和上游意图待 Linux 运行时实验确认。
- 对策：在 M1 建立布局探针和双进程测试；在结果明确前不要自行“修正”C++ 布局。

### R-004：上游测试在非 Linux 上不是可靠基线

- 事实：部分 Linux 文件没有文件名级平台排除，测试还直接依赖 OS/架构行为。
- 对策：所有正式基线、互操作和 sanitizer 证据来自 Linux；macOS 失败只记录为环境限制。

### R-005：异常输入边界检查需要加固

- 事实：控制 header 有 magic/version/type 检查，但共享内存 metadata 和部分链式 offset 读取依赖对端可信；`extractShmMetadata` 未完整验证 body 边界。
- 对策：C++ 接收端对 length、offset、加法溢出、循环链、最大 slice 数和文件大小做显式检查；兼容正常输入，不继承不安全行为。

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
