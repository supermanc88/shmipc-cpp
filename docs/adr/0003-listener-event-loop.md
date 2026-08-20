# ADR-0003：Listener 与 accepted Session 共享 event loop

- 状态：Accepted
- 日期：2026-08-20
- 对应切片：`S-0503`

## 背景

固定 Go 实现由 Listener 接受控制连接并创建服务端 Session；`net.Listener` 适配再把
Session 中的消息流包装为字节流连接。C++ 版本需要明确监听端、accepted Session、
event-loop 线程和兼容缓冲区的所有权，尤其要保证停止接受新连接不会中断已接受连接。

## 决策

1. `Listener` 是 move-only 公共 owner，提供 TCP/Unix 工厂、限时
   `accept_session()` 和幂等 `close()`。
2. Listener 创建一个共享 `EventLoop`。每个 accepted `Session` 强持有该对象；关闭
   Listener 只关闭监听 FD，最后一个 Listener/Session owner 销毁时才 stop/join。
   move-assignment 先关闭目标原有监听，再接管源 owner。
3. client Session 只允许 `open_stream()`，server Session 只允许
   `accept_stream()`；错误角色稳定返回 `unsupported`。
4. listening FD 设为 nonblocking，`poll` 最多等待 50 ms 后复查关闭状态；并发 accept
   串行化，因此 `close()` 可在有界时间内唤醒等待者。close 不 move 公共成员中的
   shared_ptr，避免与并发 accept 复制 owner 发生数据竞争。
5. accept timeout 只覆盖等待控制连接；连接建立后的协议握手同步完成。公开帧上限在
   v2/v3 入口分别收窄到协议 metadata 硬上限。
6. `StreamConnection` 是 copy-based 兼容层：一次 write 对应一条 shmipc 消息；read
   隐藏消息边界、保留未读后缀，并可连续拼接已经就绪的消息。

## 结果与取舍

Listener 关闭与 Session 生命周期解耦，且多 accepted Session 不会各自创建 epoll
线程，为 `S-0504` SessionManager 复用相同 event-loop ownership 留出一致模型。
兼容层不可提供共享内存零拷贝，并要求同一对象的 close 与其他操作由调用者同步；需要
消息所有权和零拷贝时仍应直接使用 `Stream`。
