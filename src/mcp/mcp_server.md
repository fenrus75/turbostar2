# MCP Server (`mcp_server`)

## Goals
`mcp_server` manages the lifecycle and JSON-RPC 2.0 communication for an external Model Context Protocol (MCP) server subprocess in Turbostar:
1. **Subprocess Lifecycle**: Spawns server binaries (Python, uv, npm, or custom executables) via pipes for `stdin`, `stdout`, and `stderr`, tracking process IDs and process groups.
2. **JSON-RPC Communication**: Sends structured requests and notifications, matching response IDs asynchronously using `std::promise` and `std::future`.
3. **Tool Discovery & Exposure**: Discovers tools declared by the MCP server, validates their JSON schemas, and publishes them into Turbostar's `agentlib::tool_registry`.
4. **Security & Sandboxing**: Performs pre-execution static analysis (e.g. Bandit security scanning for Python MCP servers).

## Architecture and Constraints
- **Concurrency & Locking**:
  - `state_mutex_` protects subprocess handles (`pid_`), file descriptors, and reader thread flags. It must be released before invoking blocking communication to prevent self-deadlocks.
  - `requests_mutex_` protects the pending request promise map (`pending_requests_`) and request ID counters.
- **Thread Management**:
  - Background reader and stderr threads continuously read responses and error logs from the server pipes.
  - Upon shutdown, `stop()` signals the process group (`SIGTERM`, followed by `SIGKILL` if unresponsive), closes pipes, joins threads, and resolves all outstanding promises.
- **Destructor & Noexcept Contract**:
  - `mcp_server::~mcp_server()` and `mcp_server::stop()` must be strictly `noexcept`. Destructors in C++ must never throw or propagate exceptions, as doing so during stack unwinding causes immediate `std::terminate()` aborts.

## Lessons Learned
- **Destructor Exception Escape via Promise Resolution**: In `mcp_server::stop()`, canceling pending requests fulfills each `std::promise<nlohmann::json>` with an error payload. If a promise has already been satisfied or broken by a racing reader thread, calling `std::promise::set_value()` throws `std::future_error` (`future_errc::promise_already_satisfied`). If unhandled, this exception escapes `stop()` and `~mcp_server()`, causing an immediate crash (`std::terminate`). Always wrap individual promise resolutions, JSON object allocations, logger calls, and thread joins in `try/catch` blocks inside `stop()`, guaranteeing no exceptions ever escape during destruction.
