# 文件 `include/shmipc/session_manager.hpp`

## Purpose

定义可安装的多 Session 客户端管理 API：批量 round-robin 选路、空闲 Stream 池、每 Session 断线重建，以及自动归还的 move-only RAII lease。

## Exports（全量）

- `SessionManagerConfig`：client 配置、Session 数量、每池空闲上限、轮询批量、重连间隔与健康检查间隔。
- `PooledStream`：独占 Stream lease；析构时尝试归还原池，不可安全复用时关闭。提供 `stream`、`session_index`、`return_to_pool` 与显式 `close`。
- `PooledStreamResult`：lease 与稳定公共状态。
- `SessionManager`：move-only owner；提供有效性、Session 数量、`get_stream` 与幂等 `close`。
- `SessionManagerResult`：manager 与稳定公共状态。
- `make_tcp_session_manager`、`make_unix_session_manager`：同步建立全部初始 Session；TCP 不支持 memfd。

## Ownership & Concurrency

- Manager 的共享 PImpl 持有固定池数组和每池一个监控线程；池数组初始化后不再改变，因此 `get_stream` 可与 `close` 并发。
- 每池 mutex 串行化 Session 状态、FIFO 空闲队列和 generation；选择计数使用 atomic。
- lease 弱引用来源池，不延长 Manager 生命周期。Manager 已关闭、Session generation 已变化、Stream 有 fallback/未读数据/callback/关闭状态或池已满时，lease 不进入空闲队列。
- `stream()` 只允许在有效 lease 上调用；lease 控制操作不得与对同一 Stream 的应用操作并发。

## Boundaries

- 初始建连是同步且全有或全无；任一 Session 失败时释放此前已建 Session。
- 轮询公式保持固定 Go 基线语义：第 `N` 次选择为 `(N / batch) % session_count`，计数从 1 开始。
- 每次重连给 file-mode 共享内存名称追加 PID、Session index 和 generation，避免旧 lease 持有的 mapping 与新 Session 路径冲突。
- 当前切片不实现 Go hot-restart epoch；该能力仍属于 `S-0601`。

## Evidence

- 公共类型与默认值：`include/shmipc/session_manager.hpp:13-104`。
- 池、generation 与 worker：`src/session_manager.cpp:25-210`。
- lease 回收和选路：`src/session_manager.cpp:212-342`。
- 工厂和配置边界：`src/session_manager.cpp:352-378`。
- 公共集成：`tests/public_session_manager_test.cpp` 覆盖批量轮询/FIFO 复用、池容量、TCP 断线重连、并发关闭和错误配置。
- 安装边界：`tests/package_consumer/main.cpp` 仅通过安装头验证 move-only manager/lease。

## Links

- [Session 公共 API](include__shmipc__session.hpp.md)
- [SessionManager 实现](src__session_manager.cpp.md)
- [Session 公共实现](src__session.cpp.md)
- [内部多路 Session](src__core__v2_multiplexed_session.hpp.md)
- [根目录](../dirs/root.md)
- [架构决策](../02_DECISIONS.md)
