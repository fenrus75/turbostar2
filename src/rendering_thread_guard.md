# Rendering Thread Guard (`rendering_thread_guard`)

## Goals
The `rendering_thread_guard` class (`src/rendering_thread_guard.h`, `src/rendering_thread_guard.cpp`) provides a lightweight Thread-Local Storage (TLS) mechanism to identify the active UI/rendering thread and flag accidental latency-invasive operations:
1. **Interactive Latency Protection**: Guards the UI loop from blocking operations (such as synchronous subprocesses, heavy language server queries, or synchronous disk I/O) that cause frame drops or perceived freezes.
2. **Zero-Overhead Background Checks**: Background worker threads (e.g. `git_worker`, `doc_worker`, LSP threads, highlighter threads) evaluate a single thread-local boolean read (`is_rendering_thread()`) in ~1 CPU instruction (`%fs:offset`) without locks, memory allocations, or string formatting.
3. **Easy Code Traceability**: Every warning emitted includes a distinct, required `tag` (e.g. `[command_runner]`, `[lsp_manager]`) enabling instant grepping to identify offending call sites.

## Architecture and Constraints
- **RAII Lifecycle**: Instantiated in `editor::run()` to mark the main UI thread during its execution lifetime, automatically resetting upon exit.
- **Thread Isolation**: Backed by `inline static thread_local bool is_rendering_thread_{false};`. Does not use locks or cross-thread synchronization.
- **Non-Fatal Diagnostics**: Warnings are routed via `event_logger::get_instance().log_warn()` and recorded in the exit error summary, providing clear observability without aborting execution.

## Lessons Learned
- **Early Warning vs Post-Hoc Latency Spikes**: While `latency_spike` logging captures slow events after they finish, `warn_high_latency` catches architectural violations right at the call site as soon as a heavy synchronous operation begins on the main thread.
- **Variadic In-Place Formatting**: Providing a variadic template `std::format_string<Args...>` overload ensures callers only pay formatting and allocation costs when a violation actually occurs on the rendering thread.
