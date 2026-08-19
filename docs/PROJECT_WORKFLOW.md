# shmipc-cpp 项目标准工作流

## 1. 适用范围

本文档是本项目对《软件项目端到端标准工作流程》的落地版本，规定本地开发、远程 Linux 验证、Go oracle 互操作、证据记录和提交门禁。

核心闭环：

```text
确认需求/决策 → 本地实现与快检 → rsync 到远程 Linux
→ 远程构建与自动测试 → Go↔C++ 互操作 → 记录证据
→ 自动可验收则直接本地提交 / 需人工验收则等待用户 → 用户 push → CI 复核
```

## 2. 固定环境

### 本地工作区

- 路径：`/Users/chengheming/Source/Personal/shmipc-cpp`
- 用途：唯一源码工作区、Git 操作、文档、macOS 静态检查和 Linux 交叉编译。
- 约束：macOS 不是 shmipc 的正式运行验收环境。

### 远程 Linux 工作区

- SSH 配置别名：`23.2`
- 实际连接：`root@10.210.23.2`
- 远程路径：`/home/chm/shmipc-cpp`
- 系统：Kylin Linux Advanced Server V10
- 内核：`4.19.90-20.0stable.x86_64`
- 架构：x86_64
- 已有工具：CMake 3.20.6、GCC 8.5.0、Ninja 1.8.2、GNU Make 4.2.1、rsync 3.1.3
- 当前缺失：Go、Clang、Docker、Podman

连接注意事项：直接执行 `ssh 10.210.23.2` 不会命中本机为该设备配置的 root 用户规则；统一使用 `ssh 23.2`。当前 SSH 会提示未使用 post-quantum KEX，这是环境安全告警，不影响本项目功能测试，但升级 SSH 服务端后应复核。

## 3. 同步规则

远程目录是本地仓库的 source-only 镜像，不作为独立源码来源：

- 所有源码修改只在本地完成。
- 使用 rsync 同步，不手工修改远程源码。
- 默认不使用 `--delete`，避免误删远端构建证据或未知文件。
- 不同步 `.git`、本地构建目录和缓存。
- Go submodule 以普通源文件同步；固定 commit 由本地 gitlink 和证据记录保证。

标准同步命令：

```bash
cd /Users/chengheming/Source/Personal/shmipc-cpp
rsync -az --no-times --omit-dir-times \
  --exclude='.git' \
  --exclude='build' \
  --exclude='build-*' \
  --exclude='.cache' \
  ./ 23.2:/home/chm/shmipc-cpp/
```

本机与远端时钟目前约有 2 分钟漂移。`--no-times --omit-dir-times` 是必要参数：若保留本机时间戳，Ninja 可能持续认为 `CMakeLists.txt` 或 `cmake/` 输入比刚生成的 `build.ninja` 更新，并以 `manifest 'build.ninja' still dirty` 失败。

若未来确需清理远端陈旧源码，必须先比较 `rsync --dry-run --delete` 输出，再由用户确认是否执行实际删除。

## 4. 远程构建规则

M0 建立 CMake 后，标准 Debug 快检为：

```bash
ssh 23.2 '
  set -eu
  cd /home/chm/shmipc-cpp
  cmake -S . -B build/debug -G Ninja \
    -DCMAKE_BUILD_TYPE=Debug \
    -DSHMIPC_WARNINGS_AS_ERRORS=ON
  cmake --build build/debug
  ctest --test-dir build/debug --output-on-failure
  cmake --install build/debug --prefix build/install
'
```

Release/性能相关切片使用独立目录：

```bash
ssh 23.2 '
  set -eu
  cd /home/chm/shmipc-cpp
  cmake -S . -B build/release -G Ninja -DCMAKE_BUILD_TYPE=Release
  cmake --build build/release
  ctest --test-dir build/release --output-on-failure
'
```

Sanitizer 使用独立构建目录，不能混用编译产物：`build/asan`、`build/ubsan`、`build/tsan`。可用选项为 `SHMIPC_ENABLE_ASAN`、`SHMIPC_ENABLE_UBSAN`、`SHMIPC_ENABLE_TSAN`；ASan 与 TSan 不能同时开启。

远端已安装 `libasan-8.5.0-10.el8.x86_64`，独立 ASan 构建与 CTest 已通过。`libubsan` 和 `libtsan` 仍缺失，因此 ASan+UBSan 组合与 TSan 尚不能链接。未经用户授权不继续修改系统包；在环境补齐前，本机 AppleClang 的 ASan+UBSan 仅作快速检查，不能替代正式 Linux UBSan/TSan 门禁。

### GitHub Actions 门禁

`.github/workflows/ci.yml` 在 push、pull request 和手工触发时运行：

- 常规矩阵：Ubuntu 24.04，GCC/Clang × Debug/Release，执行配置、编译、CTest 和安装。
- Sanitizer 矩阵：GCC Debug，分别执行 ASan+UBSan 和 TSan。
- Go oracle：Go 1.25.10，校验固定 submodule commit，并运行控制协议、queue/buffer layout、角色 counter 行为及 C++↔Go 双向 slice-chain 互操作测试。
- checkout 使用只读仓库权限、禁用凭据持久化，并递归拉取固定 Go submodule。

远程主机用于开发中快速复核，GitHub Actions 用于独立环境门禁；两者结果分别记录，不相互冒充。2026-08-18 首轮 GitHub Actions run `32116398237` 的六个矩阵作业全部通过，建立了 `S-0002` 云端基线。

2026-08-18，提交 `34ef510` 的 run `32119710781` 新增 Go 1.25.10 control-header oracle 后，完整七项矩阵全部通过，建立了 `S-0003` 与 M0 完成基线。

2026-08-18，提交 `c1c23f9` 的 run `32134325132` 中 GCC/Clang Debug/Release、ASan+UBSan、TSan 与生产 BufferWriter/Reader Go oracle 七项全部成功，关闭 M2 共享内存数据平面门禁。

2026-08-18，提交 `3f2db07` 的 run `32151993614` 中七项作业及关键步骤全部成功；Go oracle 在 Linux 真实执行两个方向的 v2 Session 初始化，关闭 `S-0302` 握手门禁。

2026-08-18，提交 `050d7da` 的 run `32154121843` 中七项作业及关键步骤全部成功；Go protocol oracle 14/14，关闭 `S-0303` C++ client→Go server 单 Stream 门禁。

2026-08-18，`S-0304` 候选在远端 GCC 8.5 Debug/ASan 14/14、Go client→C++ server 普通互操作 300/300 与 ASan helper 50/50 通过；等待 push 后云端七项门禁。

2026-08-19，提交 `0347f34` 的 run `32158446306` 中七项作业及关键步骤全部成功；Go protocol oracle 15/15，关闭 `S-0304` Go client→C++ server 单 Stream 门禁。

2026-08-19，提交 `78913e6` 的 run `32204938990` 中 GCC/Clang Debug/Release、ASan+UBSan、TSan 与 Go protocol oracle 七项作业全部成功；oracle CTest 16/16，关闭 `S-0305a/b` 多 Stream、deadline 与错误传播门禁，M3 正式完成。

## 5. Go 基线与互操作

Go 与 C++ 双向互操作已确认为项目正确性验证目标，但 Go 不是 C++ 库的运行依赖。

### 固定 oracle

- 仓库：`third_party/shmipc-go`
- commit：`55c241eea321071278d1ee7f7c46292d23e50a5b`
- 所有 fixture、对端二进制和报告必须记录该 commit。

Go 协议与数据平面 oracle 的标准入口：

```bash
go run tools/go_oracle/run_control_header_oracle.go
```

该 runner 使用临时 Go overlay 访问上游 package 内部的控制协议、buffer pool 与 queue，不修改 submodule；commit 不匹配、协议字节、链式 slice 或 queue 双向行为不一致都会失败。详细回归命令见 [regression-test-guide.md](regression-test-guide.md)。

### 当前 Go 工具链策略

远端暂未安装 Go。当前可从本机交叉编译纯 Go/Linux amd64 测试或 oracle 二进制，再 rsync 到远端执行：

```bash
cd third_party/shmipc-go
GOOS=linux GOARCH=amd64 go test -c \
  -o /tmp/shmipc-go-baseline-linux-amd64.test .

ssh 23.2 'mkdir -p /home/chm/shmipc-cpp/.remote-cache'
rsync -az /tmp/shmipc-go-baseline-linux-amd64.test \
  23.2:/home/chm/shmipc-cpp/.remote-cache/shmipc-go-baseline.test

ssh 23.2 '
  cd /home/chm/shmipc-cpp
  chmod +x .remote-cache/shmipc-go-baseline.test
  .remote-cache/shmipc-go-baseline.test -test.count=1 -test.timeout=120s
'
```

2026-08-18 基线：本机 Go 1.25.10 生成的 Linux/amd64 测试二进制在远端完整执行 `PASS`，退出码 0。

oracle runner 也支持直接交叉编译当前 overlay 测试：设置绝对路径环境变量 `SHMIPC_GO_ORACLE_COMPILE_LINUX_AMD64` 后执行 runner。该模式固定 `GOOS=linux GOARCH=amd64 CGO_ENABLED=0`，用于远端无 Go 环境下运行 Linux-only Go↔C++ 握手测试。

正式互操作开始前，应选择以下一种可复现方案并记录 ADR：

1. 在远端安装固定 Go 工具链；或
2. 在本地/CI 固定 Go 版本构建带版本摘要的 oracle 二进制并同步。

不得依赖未记录版本的临时 Go 二进制作为发布证据。

## 6. 单切片执行闭环

1. 在计划中选定一个 `S-*`，绑定相应 requirement IDs。
2. 检查 `git status`，识别用户已有修改。
3. 阅读相关 `arch_docs/`、上游实现和现有测试。
4. 实现代码、稳定自动断言、错误路径和必要文档。
5. 本地运行格式、静态检查、适用的交叉编译和 `git diff --check`。
6. 按第 3 节同步到远程。
7. 在远程运行对应 Debug 测试；布局、并发、内存、构建系统变化进入完整门禁。
8. 互操作切片分别运行 Go client↔C++ server 和 C++ client↔Go server，v2/v3 分开记录。
9. 若稳定自动化测试已完整覆盖验收目标，自测通过并更新 evidence 后直接创建本地候选提交，无需再次等待用户确认。
10. 连续的小切片优先积累为一批本地提交，达到稳定检查点、需要 GitHub Actions 独立证据或进入关键决策门时再请用户统一 push。
11. 若仍需要用户参与设备、交互、主观判断或其他人工测试，提供可重复的验收命令、输入和预期结果，等待用户确认后再提交。
12. 本地 commit 不等于远程授权；`git push`、发布、PR 等远程写操作仍由用户执行或逐次明确授权。远程 CI 通过后标记切片完成。

## 7. 远程证据记录

每次正式远程验证至少记录：

```text
本地 Git commit / 工作区状态
Go submodule commit
远程主机与路径
OS / kernel / architecture
CMake / compiler / Go 版本
构建类型与 sanitizer
执行命令
测试结果与耗时
报告或二进制 SHA-256
执行日期与执行者
```

`.remote-cache/` 和远程 `build/` 是临时运行资产，不是长期证据库；正式报告回写本地 `docs/`，CI/Release artifact 保存长期构建物。

## 8. 工具链决策约束

- 项目最低语言标准已确定为 C++17，与当前 GCC 8.5 对齐；升级到 C++20 必须单独评审并提供新版 GCC/Clang 或独立可复现工具链。
- 未经明确决策，不在远端直接安装/升级系统包。
- Clang/TSan、arm64 和云 CI 仍需后续环境；单台远程 x86_64 主机不能替代完整平台与编译器矩阵。
- 远程基线通过证明 Linux 主路径可运行，不自动证明 C++ 实现兼容或发布完成。
