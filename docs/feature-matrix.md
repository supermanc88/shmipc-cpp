# shmipc-cpp 功能矩阵

## 状态定义

- `已验证`：实现和规定证据均已通过。
- `部分完成`：已有独立可验证实现，但同一计划能力仍有后续子切片。
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
| amd64/arm64 queue layout golden | 已验证 | 两架构 Go oracle、C++ 双布局、run `32125329954` | 持续回归 |
| buffer manager/list/slice layout golden | 已验证 | 角色 counter probe、C++/ASan、run `32125329954` | 持续回归 |
| file/memfd RAII mapping | 已验证 | AppleClang、GCC 8.5、真实 memfd ownership、run `32129419428` | 持续回归 |
| 单进程分级 buffer pool | 已验证 | 档位回退、角色 counter/token、损坏 header、run `32129419428` | 持续回归 |
| 跨进程原子 buffer pool 与链式互操作 | 已验证 | 双进程压力、TSan、C++↔Go 双向 20,000 字节链、run `32129419428` | 持续回归 |
| MPSC queue 与 working flag | 已验证 | 线程/进程压力、唤醒竞争、双向 Go↔C++ 1,000 elements、run `32131088262` | 持续回归 |
| BufferWriter/Reader 与 pin/release | 已验证 | 单片 borrowed、跨片 owned copy、RAII、Go oracle、run `32134325132` | 持续回归 |
| Unix/TCP control socket 与 exact IO | 已验证 | partial/EOF/would-block、真实 TCP/Unix、三套本机配置、远端 GCC/ASan | 持续回归 |
| Linux epoll control dispatcher | 已验证 | ET 可消费读缓冲、写背压/串行、关闭语义；远端 GCC Debug/Release/ASan、专项 100 次、run `32148166394` | 持续回归 |
| v2 `/dev/shm` 握手 | 已验证 | 双向真实 Go Session、错误/清理路径、远端 50/50、run `32151993614` 七项门禁 | 持续回归 |
| v2 client 单 Session/Stream | 已验证 | C++/Go 20,000→17,000 字节、Polling、timeout、close、远端 ASan/50 轮、run `32154121843` | 持续回归 |
| v2 server 单 Session/Stream | 已验证 | Go client ID 2、三消息、双向 close、远端 Debug/ASan、300/300、ASan 50/50、run `32158446306` | 持续回归 |
| v2 client-originated 多 Stream | 已验证 | ID 2/3/4、并发首包、deadline、queue-full retry/close fallback、错误扇出、双向 Go 100 轮、ASan 20 轮、run `32204938990` | 持续回归 |
| v2/v3 sticky 数据 fallback | 已验证 | buffer exhaustion、shared→fallback→sticky→ACK、run `32212075730` | 持续回归 |
| v3 memfd 多路 Session | 已验证 | C++/固定 Go 双向数据面、run `32223456643` 七项门禁 | 持续回归 |
| Session fallback breaker | 已验证 | 固定 30 秒 unhealthy、拒绝新流、恢复/重开单测、v2/v3 Go 双向 50 轮与 ASan 10 轮、run `32329216783` | 持续回归 |
| 公共异步 callback executor | 部分完成 | 每流串行、跨流并行、RAII 注销、关闭等待、callback 内关闭与异常隔离；本机三套 sanitizer、远端 GCC/ASan 19/19 | 等待云端门禁 |
| 公共服务端 Listener 与字节流适配 | 部分完成 | TCP/file v2、Unix/memfd v3、Listener 关闭唤醒、accepted Session 延续、跨消息 read；本机/远端 sanitizer 20/20 | 等待云端门禁 |
| SessionManager、Stream pool 与重连 | 部分完成 | batched round-robin、RAII lease、有界 FIFO、generation 重连、并发 close/get；本机四套与远端 Debug/ASan 21/21、专项 20 轮、安装消费者 | 提交和云端验证 |

## 产品需求

| Requirement IDs | 能力 | 状态 | 首个实现切片 |
|---|---|---|---|
| `COMP-001` | Go↔C++ v2 双向互通 | 握手、两个方向单 Stream、client-originated 多 Stream、deadline 与错误扇出已由 run `32204938990` 验证 | M3 |
| `S-0301` | Unix/TCP control transport 与 epoll | 已验证；run `32148166394` 七项门禁成功 | M3 |
| `COMP-002` | Go↔C++ v3 双向互通 | 版本协商、memfd 握手及多路 shared/fallback/close 数据面已由 run `32223456643` 验证 | M4 |
| `PROTO-001` | 控制头、事件、metadata 与 fallback 编解码 | 已验证 | `S-0101` |
| `PROTO-002` | v2/v3 初始化状态机 | v2/v3 均已完成本机、远端与固定 Go 双向验证 | `S-0302`/`S-0401` |
| `SHM-001` | Queue/buffer manager/slice 共享布局与映射 | byte layout 与 mapping 已云端验证 | `S-0102..0103`/`S-0201` |
| `SHM-002` | 分级 buffer 分配回收 | 单进程与双进程原子路径已云端验证 | `S-0202..0203` |
| `SHM-003` | 链式 slice 与零拷贝生命周期 | publish/adopt/recycle 与 Reader/Writer pin/release 已由 run `32134325132` 验证 | `S-0203`/`S-0205` |
| `SHM-004` | 损坏 offset/length 防护 | 9 类固定 corpus 与 run `32125329954` 已验证 | `S-0104` |
| `QUEUE-001` | MPSC queue put/pop | 已云端验证 | `S-0102`/`S-0204` |
| `QUEUE-002` | working flag 与批量唤醒 | 已云端验证 | `S-0204` |
| `QUEUE-003` | amd64/arm64 原子对齐 | 两套布局与两架构原子路径已云端验证 | `S-0102`/`S-0204` |
| `STREAM-001..004` | 多路复用、读写、fallback、关闭 | v2/v3 多 Stream、deadline、sticky fallback、关闭及 Session breaker 已完成本机/远端验证 | M3/M4 |
| `API-001` | RAII Session/Stream API | 公共 client API、PImpl、同步示例及 v2/v3 Linux 集成已通过本机/远端验证，待云端门禁 | `S-0501` |
| `API-002` | Listener 与 SessionManager | Listener、服务端 Session、StreamConnection、SessionManager/Stream pool/断线重建均已完成本机/远端完整验证，待云端门禁 | M5 |
| `API-003` | 异步 callback API | 共享 executor、每流串行 pump、RAII subscription 和关闭/异常生命周期已完成本机/远端验证，待云端门禁 | `S-0502` |
| `OPS-001` | 热重启 | 未开始 | `S-0601` |
| `OBS-001` | 指标与日志 | 未开始 | M5 |
| `NFR-001` | Linux x86_64/arm64 | x86_64 构建基线已验证 | M2 起补 arm64 |
| `NFR-002` | 内存与并发安全 | 门禁已验证，功能待实现 | 全程 |
| `NFR-003` | 性能目标 | 未开始 | M5 |
| `NFR-004` | 安装与消费 | macOS/Linux install 后独立 `find_package` 消费者已验证，CI 门禁已补，待云端结果 | `S-0501` |

本表只表达可复核状态；完整验收标准见 [SHMIPC_CPP_PORTING_PLAN.md](SHMIPC_CPP_PORTING_PLAN.md)。
