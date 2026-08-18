# shmipc-cpp 回归测试指南

## 本机快速回归

```bash
cmake -S . -B build/debug -DCMAKE_BUILD_TYPE=Debug \
  -DSHMIPC_WARNINGS_AS_ERRORS=ON
cmake --build build/debug --parallel
ctest --test-dir build/debug --output-on-failure
git diff --check
```

## Go 协议与数据平面 oracle

需要 Go 1.20+、Git 和已初始化的 submodule：

```bash
go run tools/go_oracle/run_control_header_oracle.go
```

运行器首先要求 `third_party/shmipc-go` HEAD 严格等于 `55c241eea321071278d1ee7f7c46292d23e50a5b`，然后通过临时 overlay 把 oracle test 注入上游 package。它直接调用上游控制协议、buffer pool、queue 和真实 Session 初始化，并用 C++ helpers 做双向验证，不会修改 submodule 工作区。v2 Session 子项仅在 Linux 执行。

也可通过 CTest 运行完整本机集合：

```bash
cmake -S . -B build/oracle -DCMAKE_BUILD_TYPE=Debug \
  -DSHMIPC_WARNINGS_AS_ERRORS=ON \
  -DSHMIPC_ENABLE_GO_ORACLE_TESTS=ON
cmake --build build/oracle --parallel
ctest --test-dir build/oracle --output-on-failure
```

## 远端 Linux 回归

按 [PROJECT_WORKFLOW.md](PROJECT_WORKFLOW.md) 使用不保留时间戳的 rsync 同步，然后运行：

```bash
ssh 23.2 '
  set -eu
  cd /home/chm/shmipc-cpp
  cmake -S . -B build/debug -G Ninja \
    -DCMAKE_BUILD_TYPE=Debug \
    -DSHMIPC_WARNINGS_AS_ERRORS=ON
  cmake --build build/debug
  ctest --test-dir build/debug --output-on-failure
'
```

远端尚无 Go，因此默认只执行 C++ tests。需要运行 Linux-only v2 oracle 时，可由 runner 交叉编译带 overlay 的测试二进制：

```bash
SHMIPC_GO_ORACLE_COMPILE_LINUX_AMD64="$PWD/build/v2-oracle-linux-amd64" \
  go run tools/go_oracle/run_control_header_oracle.go
rsync -az --no-times --omit-dir-times build/v2-oracle-linux-amd64 \
  23.2:/home/chm/shmipc-cpp/build/v2-oracle-linux-amd64
ssh 23.2 'cd /home/chm/shmipc-cpp && \
  SHMIPC_CPP_V2_HANDSHAKE_HELPER=$PWD/build/debug/tests/shmipc_v2_handshake_interop_helper \
  ./build/v2-oracle-linux-amd64 -test.run "^TestV2HandshakeInterop$" -test.v'
```

## 结果判定

- `shmipc.version`：公共头与链接库版本一致。
- `shmipc.control_header_golden`：生产 codec 完整消费 10 类控制事件 fixture，字段与大端字节一致。
- `shmipc.protocol_codec`：metadata/fallback 正反向 golden 一致，并拒绝截断、尾随字节、非法 magic/version/type、错误 payload 事件、超长字段和超过配置上限的帧。
- `shmipc.queue_layout`：C++ 同时验证 amd64/arm64 header offsets、12 字节 element、region size、越界 slot、截断和 arm64 manager 对齐限制。
- `shmipc.shared_queue`：4 producer/单 consumer 并发顺序、full/empty、batch、父子进程环绕及 working flag 竞争窗口。
- `shmipc.buffer_layout`：manager/list/slice offsets、region size、creator/mapper counter 字段及截断/非法字段检查。
- `shmipc.shared_memory_region`：file 双视图、move/unlink 生命周期，以及 Linux memfd 的 borrowed/transferred FD 所有权；非 Linux 明确验证 unsupported。
- `shmipc.buffer_pool`：tier 配置与排序、原子分配回收、父子进程并发压力、双向 chain publish/adopt、角色净 counter，以及损坏 head/tail/size/used-length 防护。
- `shmipc.buffer_io`：reserve/write/publish、单 slice borrowed view、跨 slice owned copy、peek/byte/string/discard、pin/release、逐 slice 推进、越界和 RAII 回收。
- `shmipc.control_socket`：adopt/move ownership、partial exact IO、EOF/would-block、真实 loopback TCP、pathname Unix socket、重复 bind 与路径清理。
- `shmipc.epoll_dispatcher`：Linux 上验证 ET partial frame 保留、writev、EAGAIN 背压、并发写无交错、remote/local/shutdown close、buffer/callback 错误与重入 close；非 Linux 明确验证 unsupported。
- `shmipc.v2_handshake`：验证 client/server 成功初始化、queue 方向翻转、两角色 buffer 分配回收、错误版本/事件、截断 body、缺失路径、重复文件保护与失败清理。
- `shmipc.go_protocol_oracle`：除控制协议与布局外，调用真实 C++ helpers 双向传递 20,000 字节 slice chain、各 1,000 个 queue elements，并在 Linux 验证两个方向的 v2 Session 初始化。
- 任一 commit mismatch、缺行、重复/错序事件或字节差异均为失败，不允许自动更新 golden 后绕过评审。

当前 golden 的 SHA-256：

- `control_headers.txt`：`ee6379a976c47c4d81c894ecf110132884ee8e48086091338cb17a8d8765fdfa`
- `shm_metadata.txt`：`5e5e66b2563feb9d0b96f4c5cdb5922cd72e87b6bbfec4b49965e7b1bc5f1fba`
- `fallback_data.txt`：`ce97f80676a92066b62b0127d0ae5561ed7be2bc8035c88c98fc79db82b77f94`
- `queue_layout.txt`：`3c2dba47b214fe158582c7cb31ec9b74fa060819d848a87b253c1cf83d721697`
- `buffer_layout.txt`：`83a090638c0096c7619c66f22b6621ae6da6b77343150bacbfde4a99d6b6af5b`

固定损坏布局 corpus 位于 `tests/data/corpus/layout_corruption.txt`，SHA-256 为 `342f559d8106b538e8b41bc562041bb9dbaeb34b3e969275daccc1fc560149e5`。测试从合法三节点链应用每条变异，validator 必须返回 corpus 指定错误且不得越界或无限循环。
