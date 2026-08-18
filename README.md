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

## Install

```bash
cmake --install build/debug --prefix /path/to/prefix
```

The installed package exports the target `shmipc::shmipc` for use with
`find_package(shmipc CONFIG REQUIRED)`.

## Project documents

- [Porting plan](docs/SHMIPC_CPP_PORTING_PLAN.md)
- [Project workflow](docs/PROJECT_WORKFLOW.md)
- [Architecture index](arch_docs/00_INDEX.md)
