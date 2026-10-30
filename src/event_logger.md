# Event Logger (`event_logger`)

## Goals
The `event_logger` class provides high-performance, structured diagnostic logging across all Turbostar subsystems. It records system events, compiler outputs, LSP server communications, and tool traces, timestamping entries relative to process start time. It also features a specialized lock-free signal-safe ring buffer for post-mortem forensics during fatal crashes, log-level categorization (`debug`, `info`, `warn`, `error`, `fatal`), and session error tracking for exit diagnostics.

## Architecture and Constraints
- **Singleton Lifecycle**: Accessed via `event_logger::get_instance()`.
- **Log Levels & Filtering**:
  - Defined by `enum class log_level : uint8_t { debug, info, warn, error, fatal }`.
  - Configurable minimum log level (`set_min_level()`, defaulting to `debug`). Templated logging overloads evaluate `min_level_` upfront and early-exit prior to calling `std::format`, eliminating heap allocations and string formatting costs for filtered messages.
  - Dedicated ergonomic helpers: `log_debug()`, `log_info()`, `log_warn()`, `log_warning()`, `log_error()`, `log_fatal()`.
  - Full backwards compatibility: unadorned `log(message)` and `log(fmt, args...)` map to `log_level::info`.
- **Concurrency & File Streaming**:
  - File writes (`log_stream_`), in-memory event histories (`events`), and recent error tracking (`recent_errors_`) are guarded by `mutable std::mutex mutex_`.
  - Entries are formatted with relative millisecond timestamp and level prefix: `[{:06d}ms] [{LEVEL}] {message}`.
- **Signal-Safe Post-Mortem Logging**:
  - Maintains a fixed-size circular ring buffer (`log_ring_slot ring_slots_[16]`) with atomic sequences (`std::atomic<uint64_t> ring_write_seq_`).
  - Implements `dump_recent_logs_signal_safe(int fd, size_t max_count)`, which writes the last 16 log entries directly to a file descriptor using raw POSIX `write()` syscalls without acquiring mutexes or calling `malloc()`.
- **Exit Error Diagnostics**:
  - Tracks errors and fatals in a bounded circular buffer (`recent_errors_`, up to 16 records) alongside an atomic error counter (`total_errors_logged_`).
  - `print_exit_error_report()` prints a concise terminal report of the final errors on application exit (alongside interactive latency metrics). If zero errors occurred during the session, it stays completely silent with zero output.
- **MCP & Headless Mode Discipline**:
  - In stdio MCP mode (`turbomcp`) or headless server mode, stdout logging must be explicitly disabled via `enable_stdout_logging(false)`. Writing log text to stdout corrupts JSON-RPC message framing.

## Lessons Learned
- **Crash Forensic Trails**: During segmentation faults or assertion aborts, regular buffered loggers fail to flush to disk, leaving developers without context on what event caused the crash. The atomic signal-safe ring buffer guarantees recent execution context is recorded directly into crashdump reports.
- **Protocol Framing Purity**: Diagnostic outputs directed to `stdout` break Model Context Protocol (MCP) clients with JSON parse errors. Centralizing logging through `event_logger` ensures all logging routes to disk files (`session.log`) in headless environments.
- **Silent Background Errors**: Treating all logs equally led to critical background failures (LSP disconnections, bad configurations, worktree errors) being buried in verbose debug logs. Tracking errors separately and surfacing the last 5 errors upon normal application exit provides immediate visibility for developers without requiring manual log inspections.
