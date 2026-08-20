# 文件 `src/session.cpp`

## Purpose

实现公共 client/server Session 与同步操作适配层：归一化内部错误，校验 client 配置，选择 v2 file 或 v3 memfd handshake，并以共享 EventLoop 持有内部 Session。异步执行器实现在 `src/callback.cpp`，服务监听实现在 `src/listener.cpp`。

## Key Symbols

- `map_transport_status`、`map_session_status`、`map_v3_handshake_status`：内部错误到稳定 public `Status` 的窄映射。
- `valid_config`、`map_tiers`：在连接前检查名称、容量、tier 百分比/重复 capacity，并转换内部 pool 配置。
- `Stream::close`：发布公共关闭状态、关闭内部 Stream，并在普通线程等待已注册 callback 完成。
- `Stream::is_open`：同时检查公共 close 标志与核心连接/Stream 状态，供池化 lease 和应用观察生命周期。
- `Session::Impl`：持有共享 EventLoop 与 client/server 内部 Session variant，并提供角色判断。
- `Session::start`：接管原生 socket descriptor，立即恢复为 `ControlSocket` RAII owner，启动 dispatcher，并按 mode 选择 v2/v3 client Session。
- `connect_tcp/connect_unix`：连接前配置/mode 门禁和 control socket 创建入口。

## Control Flow

1. public connect 工厂验证配置；TCP+memfd 立即返回 unsupported。
2. transport 创建 control socket，释放 descriptor 给私有 `Session::start`；start 立即 adopt，异常/失败路径仍由 RAII close。
3. 启动 dispatcher，file/memfd 分别构造内部 v2/v3 配置并运行 handshake。
4. 成功后 PImpl 共享持有 EventLoop 并接管内部 Session；失败时局部 owner 自动 stop/close/unmap。
5. client `open_stream` 或 server `accept_stream` 将内部 move-only Stream 包入 shared public PImpl；错误角色返回 unsupported。
6. `Session::close` 先关闭内部连接和 Stream state，再释放自己的 EventLoop owner；最后一个 owner 才 stop/join。重复 close 成功。

## Invariants

- 本文件是 public API 到内部 engine 的唯一生产依赖方向；公共头不 include `src/` 中任何文件。
- 原生 descriptor 的裸值只跨越一次同步函数调用，并在 `Session::start` 首句重新纳入 RAII。
- v2/v3 当前共享同一内部多路类型是实现细节，public layout 与名称不依赖该别名。
- Listener 与多个 accepted Session 可共享 EventLoop；Listener 关闭不停止仍被 Session 持有的 dispatcher。
- `Stream::Impl` 的唯一结构定义位于 `src/public/session_impl.hpp`，同步层与 callback 层共享，公共头仍只暴露不完整类型。

## Evidence

- 错误/配置映射：`src/session.cpp:14-117`。
- client/server 角色与 close：`src/session.cpp:281-321`。
- mode 启动和连接：`src/session.cpp:324-412`。
- EventLoop/Session variant：`src/public/session_impl.hpp:44-75`。
- `tests/public_session_test.cpp`：真实 Linux v2/v3 round-trip、错误 surface 与 RAII 清理。
- `CMakeLists.txt:12-81`、`cmake/shmipcConfig.cmake.in`：Threads public dependency、构建/安装入口。

## Links

- [公共头](include__shmipc__session.hpp.md)
- [内部多路 Session](src__core__v2_multiplexed_session.hpp.md)
- [异步 callback 实现](src__callback.cpp.md)
- [共享 Stream PImpl](src__public__session_impl.hpp.md)
- [Listener 实现](src__listener.cpp.md)
- [SessionManager 实现](src__session_manager.cpp.md)
- [core 目录](../dirs/src__core.md)
- [架构决策](../02_DECISIONS.md)
