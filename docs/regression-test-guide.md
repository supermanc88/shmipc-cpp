# shmipc-cpp 回归测试指南

## 本机快速回归

```bash
cmake -S . -B build/debug -DCMAKE_BUILD_TYPE=Debug \
  -DSHMIPC_WARNINGS_AS_ERRORS=ON
cmake --build build/debug --parallel
ctest --test-dir build/debug --output-on-failure
git diff --check
```

## Go control-protocol oracle

需要 Go 1.20+、Git 和已初始化的 submodule：

```bash
go run tools/go_oracle/run_control_header_oracle.go
```

运行器首先要求 `third_party/shmipc-go` HEAD 严格等于 `55c241eea321071278d1ee7f7c46292d23e50a5b`，然后通过临时 overlay 把 oracle test 注入上游 package。它直接调用上游 header、共享内存 metadata 和 fallback 编码器，不会修改 submodule 工作区。

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

远端尚无 Go，因此默认只执行 C++ tests；Go oracle 由本机和 GitHub Actions 的独立作业承担。

## 结果判定

- `shmipc.version`：公共头与链接库版本一致。
- `shmipc.control_header_golden`：生产 codec 完整消费 10 类控制事件 fixture，字段与大端字节一致。
- `shmipc.protocol_codec`：metadata/fallback 正反向 golden 一致，并拒绝截断、尾随字节、非法 magic/version/type、错误 payload 事件、超长字段和超过配置上限的帧。
- `shmipc.queue_layout`：C++ 同时验证 amd64/arm64 header offsets、12 字节 element、region size、越界 slot、截断和 arm64 manager 对齐限制。
- `shmipc.buffer_layout`：manager/list/slice offsets、region size、creator/mapper counter 字段及截断/非法字段检查。
- `shmipc.go_protocol_oracle`：固定 Go 上游验证控制协议、当前运行架构 queue、buffer 布局及 creator/mapper counter 的实际 pop/push 行为。
- 任一 commit mismatch、缺行、重复/错序事件或字节差异均为失败，不允许自动更新 golden 后绕过评审。

当前 golden 的 SHA-256：

- `control_headers.txt`：`ee6379a976c47c4d81c894ecf110132884ee8e48086091338cb17a8d8765fdfa`
- `shm_metadata.txt`：`5e5e66b2563feb9d0b96f4c5cdb5922cd72e87b6bbfec4b49965e7b1bc5f1fba`
- `fallback_data.txt`：`ce97f80676a92066b62b0127d0ae5561ed7be2bc8035c88c98fc79db82b77f94`
- `queue_layout.txt`：`3c2dba47b214fe158582c7cb31ec9b74fa060819d848a87b253c1cf83d721697`
- `buffer_layout.txt`：`83a090638c0096c7619c66f22b6621ae6da6b77343150bacbfde4a99d6b6af5b`

固定损坏布局 corpus 位于 `tests/data/corpus/layout_corruption.txt`，SHA-256 为 `342f559d8106b538e8b41bc562041bb9dbaeb34b3e969275daccc1fc560149e5`。测试从合法三节点链应用每条变异，validator 必须返回 corpus 指定错误且不得越界或无限循环。
