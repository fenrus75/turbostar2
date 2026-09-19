# Event Logger (`event_logger`)

## Goals
The `event_logger` class provides high-performance, structured diagnostic logging across all Turbostar subsystems. It records system events, compiler outputs, LSP server communications, and tool traces, timestamping entries relative to process start time. It also features a specialized lock-free signal-safe ring buffer for post-mortem forensics during fatal crashes.

## Architecture and Constraints
- **Singleton Lifecycle**: Accessed via `event_logger::get_instance()`.
- **Concurrency & File Streaming**:
  - File writes (`log_stream_`) and in-memory event histories are guarded by `mutable std::mutex mutex_`.
  - Supports formatted logging with C++20 `std::format` via template variadics (`log(fmt, args...)`).
- **Signal-Safe Post-Mortem Logging**:
  - Maintains a fixed-size circular ring buffer (`log_ring_slot ring_slots_[16]`) with atomic sequences (`std::atomic<uint64_t> ring_write_seq_`).
  - Implements `dump_recent_logs_signal_safe(int fd, size_t max_count)`, which writes the last 16 log entries directly to a file descriptor using raw POSIX `write()` syscalls without acquiring mutexes or calling `malloc()`.
- **MCP & Headless Mode Discipline**:
  - In stdio MCP mode (`turbomcp`) or headless server mode, stdout logging must be explicitly disabled via `enable_stdout_logging(false)`. Writing log text to stdout corrupts JSON-RPC message framing.

## Lessons Learned
- **Crash Forensic Trails**: During segmentation faults or assertion aborts, regular buffered loggers fail to flush to disk, leaving developers without context on what event caused the crash. The atomic signal-safe ring buffer guarantees recent execution context is recorded directly into crashdump reports.
- **Protocol Framing Purity**: Diagnostic outputs directed to `stdout` break Model Context Protocol (MCP) clients with JSON parse errors. Centralizing logging through `event_logger` ensures all logging routes to disk files (`session.log`) in headless environments.
