# 文件 `src/listener.cpp`

## Purpose

实现公共 Listener：建立 nonblocking control listener、限时 accept、启动 v2/file 或 v3/memfd 服务端 Session，并管理共享 event loop 生命周期。

## Key Symbols

- `Listener::Impl`：监听 FD、共享 `EventLoop`、mode、端口、关闭原子和 accept/listener mutex。
- `Listener::accept_session`：以最长 50 ms poll slice 等待连接，序列化 accept，随后同步握手并返回 server Session。
- `Listener::close`：原子标记关闭并关闭监听 FD，不释放 accepted Session 持有的 event loop。
- `listen_tcp`、`listen_unix`：配置门禁、dispatcher/listener 创建和 PImpl 组装。

## Invariants

- accepted Session 强持有共享 EventLoop；最后一个 owner 析构才 stop/join。
- v2/v3 的公开握手上限在协议入口分别 clamp 到 metadata 硬上限。
- listener_mutex 防止 close 与 native_handle/accept 的 FD 生命周期竞争；accept_mutex 禁止并发 accept 握手交错。
- close 只复制而不改写公共对象中的 shared_ptr owner，使其与并发 accept 复制 owner 无数据竞争。
- move-assignment 先 close 目标原有监听，再接管源 owner，避免旧 accept 因共享状态仍存活而继续等待。

## Evidence

- 状态与 ownership：`src/listener.cpp:58-77`。
- poll/accept/协议启动：`src/listener.cpp:97-187`。
- close 和工厂：`src/listener.cpp:190-259`。
- `tests/public_listener_test.cpp:119-223`：真实 Linux v2/v3、关闭延续和唤醒。

## Links

- [Listener 公共头](include__shmipc__listener.hpp.md)
- [Session 实现](src__session.cpp.md)
- [共享 PImpl](src__public__session_impl.hpp.md)
