# Crashdump Manager (`crashdump_manager`)

## Goals
The `crashdump_manager` class automatically tracks, analyzes, enriches, and reports fatal application crashes (SIGSEGV, SIGABRT, SIGFPE, SIGBUS) generated during test executions, compiler invocations, or child process runs. It bridges raw low-level kernel and unwinder dumps into structured, readable diagnostic reports for both developers and AI agents.

## Architecture and Constraints
- **Low-Level Capture Integration**: Works in tandem with `libturbocatch.so`, which catches fatal signals, captures CPU registers, parses `/proc/self/maps`, and generates mini coredumps in `/tmp/turbostar-crashes-<uid>`.
- **Hybrid GDB Backtrace Enrichment**:
  - When coredump files are available and headless GDB is installed, `crashdump_manager` runs non-interactive GDB commands (`--batch -ex "bt 35"`) to reconstruct complete function call signatures, parameter values, and precise source file lines.
  - Falls back gracefully to the raw unwinder table (`backtrace.txt`) if GDB or coredumps are missing.
- **Token Minimization & Output Sanitization**:
  - Automatically collapses contiguous crash-handling trampoline frames (`0-4`) into a single summary row to highlight the actual faulting frame.
  - Strips `<optimized out>` argument strings and trailing whitespace across all backtrace frames to eliminate token bloat for LLM attention windows.
  - Classifies source locations into `<libc>`, `<turbocatch>`, `<external>`, or project-relative workspace paths.
- **Concurrency & Thread Safety**:
  - Singleton accessed via `crashdump_manager::get_instance()`.
  - Guarded by `mutable std::mutex mutex_`. Query methods return vectors and strings by value under lock to prevent data races with concurrent test runs.

## Lessons Learned
- **Thread-Safe Container Copies**: Returning const references to internal crash vectors caused race conditions when background test suites discovered new crashes while UI rendering threads formatted tables. Query methods must return copies under lock.
- **Deterministic Crash Resolution**: Directory iteration order (`std::filesystem::directory_iterator`) is non-deterministic across filesystems. When selecting the "latest" crash without an explicit ID, sorting by mtime or numeric crash identifier is essential for repeatable debugging.
- **Clean Signal Trampolines**: Presenting raw signal handler frames to AI models causes them to attempt debugging the signal catcher rather than the code that caused the crash. Collapsing and tagging frames as `crash handling` keeps the model focused on user code.
