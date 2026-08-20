# 文件 `src/session.cpp`

## Purpose

实现公共同步 client API 的 PImpl 适配层：归一化内部错误，校验公共配置，选择 v2 file 或 v3 memfd handshake，并集中拥有内部 Session 与 epoll dispatcher。

## Key Symbols

- `map_transport_status`、`map_session_status`、`map_v3_handshake_status`：内部错误到稳定 public `Status` 的窄映射。
- `valid_config`、`map_tiers`：在连接前检查名称、容量、tier 百分比/重复 capacity，并转换内部 pool 配置。
- `Stream::Impl`：唯一拥有内部 `core::V2Stream`。
- `Session::Impl`：按声明顺序持有 dispatcher 和内部多路 client Session，使逆序析构先关闭 Session、后 join dispatcher。
- `Session::start`：接管原生 socket descriptor，立即恢复为 `ControlSocket` RAII owner，启动 dispatcher，并按 mode 选择 v2/v3 client Session。
- `connect_tcp/connect_unix`：连接前配置/mode 门禁和 control socket 创建入口。

## Control Flow

1. public connect 工厂验证配置；TCP+memfd 立即返回 unsupported。
2. transport 创建 control socket，释放 descriptor 给私有 `Session::start`；start 立即 adopt，异常/失败路径仍由 RAII close。
3. 启动 dispatcher，file/memfd 分别构造内部 v2/v3 配置并运行 handshake。
4. 成功后 PImpl 同时接管 dispatcher 和内部 Session；失败时局部 owner 自动 stop/close/unmap。
5. `open_stream` 将内部 move-only Stream 包入 public PImpl；send/receive/deadline/close 只做状态归一化与转发。
6. `Session::close` 先关闭内部连接和 Stream state，再 stop/join dispatcher，最后释放 PImpl；重复 close 成功。

## Invariants

- 本文件是 public API 到内部 engine 的唯一生产依赖方向；公共头不 include `src/` 中任何文件。
- 原生 descriptor 的裸值只跨越一次同步函数调用，并在 `Session::start` 首句重新纳入 RAII。
- v2/v3 当前共享同一内部多路类型是实现细节，public layout 与名称不依赖该别名。
- dispatcher 成员先声明、Session 后声明；默认逆序析构与显式 close 的顺序一致。

## Evidence

- 错误/配置映射：`src/session.cpp:14-117`。
- PImpl ownership：`src/session.cpp:121-134,243-281`。
- mode 启动和连接：`src/session.cpp:283-369`。
- `tests/public_session_test.cpp`：真实 Linux v2/v3 round-trip、错误 surface 与 RAII 清理。
- `CMakeLists.txt:12-81`、`cmake/shmipcConfig.cmake.in`：Threads public dependency、构建/安装入口。

## Links

- [公共头](include__shmipc__session.hpp.md)
- [内部多路 Session](src__core__v2_multiplexed_session.hpp.md)
- [core 目录](../dirs/src__core.md)
- [架构决策](../02_DECISIONS.md)
