# shmipc-cpp

`shmipc-cpp` is a C++17 reimplementation of
[`cloudwego/shmipc-go`](https://github.com/cloudwego/shmipc-go). The project is
currently under development and uses the pinned Go implementation in
`third_party/shmipc-go` as its interoperability oracle.

## Requirements

- CMake 3.16 or newer
- A C++17 compiler
- Linux for shared-memory IPC runtime development and validation

## Build and test

```bash
cmake -S . -B build/debug -DCMAKE_BUILD_TYPE=Debug
cmake --build build/debug
ctest --test-dir build/debug --output-on-failure
```

Use `-DSHMIPC_WARNINGS_AS_ERRORS=ON` to make warnings fatal. Sanitizer builds
can enable `SHMIPC_ENABLE_ASAN`, `SHMIPC_ENABLE_UBSAN`, or
`SHMIPC_ENABLE_TSAN`; AddressSanitizer and ThreadSanitizer are intentionally
mutually exclusive.

To validate the control-protocol golden fixtures against the pinned Go implementation:

```bash
go run tools/go_oracle/run_control_header_oracle.go
```

## Install

```bash
cmake --install build/debug --prefix /path/to/prefix
```

The installed package exports the target `shmipc::shmipc` for use with
`find_package(shmipc CONFIG REQUIRED)`.

## Synchronous client API

The public move-only `shmipc::Session` and `shmipc::Stream` API is declared in
`<shmipc/session.hpp>`. The `shmipc_synchronous_client` example connects to a
v2 TCP server, opens a stream, sends one message, receives its response, and
closes both RAII handles:

```bash
./build/debug/examples/shmipc_synchronous_client \
  127.0.0.1 9000 /dev/shm/my_queue /dev/shm/my_buffer hello
```

Use `shmipc::connect_unix` with `SharedMemoryMode::memfd` for the Linux v3
memfd path. Memfd descriptor transfer is intentionally unavailable over TCP.

## Synchronous server API

`<shmipc/listener.hpp>` provides move-only TCP and Unix listeners. A client
Session opens streams, while an accepted server Session accepts them. Closing
the Listener stops new accepts without invalidating accepted Sessions. The
`shmipc_synchronous_server` example accepts one v2 TCP stream and echoes one
request:

```bash
./build/debug/examples/shmipc_synchronous_server 127.0.0.1 9000
```

`<shmipc/stream_connection.hpp>` optionally adapts a message-oriented Stream
to copy-based byte reads and writes while preserving unread message suffixes.

## Session manager API

`<shmipc/session_manager.hpp>` provides a move-only multi-Session client with
batched round-robin selection, bounded FIFO Stream reuse, and independent
reconnection for each Session. `get_stream()` returns a move-only
`PooledStream` lease; destroying the lease returns a safe Stream to its source
pool and closes fallback, unread, callback-bound, stale-generation, or excess
Streams instead. Hot-restart epoch support is planned separately.

## Project documents

- [Porting plan](docs/SHMIPC_CPP_PORTING_PLAN.md)
- [Project workflow](docs/PROJECT_WORKFLOW.md)
- [Regression test guide](docs/regression-test-guide.md)
- [Feature matrix](docs/feature-matrix.md)
- [Architecture index](arch_docs/00_INDEX.md)
