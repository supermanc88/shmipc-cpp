# shmipc-cpp 功能矩阵

## 状态定义

- `已验证`：实现和规定证据均已通过。
- `基线锁定`：上游事实、fixture 或验收方法已固定，但 C++ 功能尚未实现。
- `未开始`：尚无可验收实现。

## 构建与验证基础设施

| 能力 | 状态 | 当前证据 | 后续动作 |
|---|---|---|---|
| C++17 library/test/install 骨架 | 已验证 | `S-0001`，提交 `23e7043` | 随公共 API 演进持续回归 |
| GCC/Clang Debug/Release CI | 已验证 | `S-0002`，run `32116398237` | 保持 required check |
| ASan+UBSan/TSan CI | 已验证 | `S-0002`，run `32116398237` | 核心内存/并发切片强制进入门禁 |
| 固定 Go oracle commit 校验 | 已验证 | run `32119710781` Go 1.25.10 作业 | 升级 submodule 时强制重验 |
| control-header golden | 已验证 | Go/C++/远端/云端共同验证 | 持续作为生产 codec 回归输入 |
| metadata/fallback golden | 已验证 | 固定 Go 编码器、C++ round-trip、run `32122127419` | 持续回归 |
| amd64/arm64 queue layout golden | 本地与远端已验证 | 两架构 Go oracle、C++ 双布局、GCC 8.5/ASan | 随下一批 push 补云端证据 |
| buffer manager/list/slice layout golden | 本地与远端已验证 | 两架构 Go oracle、角色 counter probe、C++/ASan | 随 M1 批次补云端证据 |

## 产品需求

| Requirement IDs | 能力 | 状态 | 首个实现切片 |
|---|---|---|---|
| `COMP-001` | Go↔C++ v2 双向互通 | 基线锁定 | M3 |
| `COMP-002` | Go↔C++ v3 双向互通 | 基线锁定 | M4 |
| `PROTO-001` | 控制头、事件、metadata 与 fallback 编解码 | 已验证 | `S-0101` |
| `PROTO-002` | v2/v3 初始化状态机 | 未开始 | `S-0401` |
| `SHM-001` | Queue/buffer manager/slice 共享布局 | 显式 byte layout 已验证，待云端 | `S-0102..0103` |
| `SHM-002..003` | 分配回收与零拷贝生命周期 | 未开始 | M2 |
| `SHM-004` | 损坏 offset/length 防护 | 9 类固定 corpus 本机/远端已验证，待云端 | `S-0104` |
| `QUEUE-001` | MPSC queue put/pop | 布局访问器已验证，并发算法未开始 | `S-0102`/`S-0204` |
| `QUEUE-002` | working flag 与批量唤醒 | 字段布局已验证，状态算法未开始 | `S-0204` |
| `QUEUE-003` | amd64/arm64 原子对齐 | byte layout 已验证，原子访问待实现 | `S-0102`/`S-0204` |
| `STREAM-001..004` | 多路复用、读写、fallback、关闭 | 未开始 | M3/M4 |
| `API-001` | RAII Session/Stream API | 未开始 | M3 |
| `API-002..003` | Listener/SessionManager/异步 API | 未开始 | M5 |
| `OPS-001` | 热重启 | 未开始 | M5 |
| `OBS-001` | 指标与日志 | 未开始 | M5 |
| `NFR-001` | Linux x86_64/arm64 | x86_64 构建基线已验证 | M2 起补 arm64 |
| `NFR-002` | 内存与并发安全 | 门禁已验证，功能待实现 | 全程 |
| `NFR-003` | 性能目标 | 未开始 | M5 |
| `NFR-004` | 安装与消费 | install 已验证，外部消费者待补 | M5 |

本表只表达可复核状态；完整验收标准见 [SHMIPC_CPP_PORTING_PLAN.md](SHMIPC_CPP_PORTING_PLAN.md)。
