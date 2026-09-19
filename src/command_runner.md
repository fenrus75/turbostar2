# Command Runner (`command_runner`)

## Goals
The `command_runner` class provides a secure, monitored subprocess execution engine for Turbostar. It executes external build scripts, compilers, test suites, and shell commands under strict Linux isolation profiles to prevent accidental host system modifications, data exfiltration, or shell injection vulnerabilities.

## Architecture and Constraints
- **Security Profiles**:
  - `apply_default_profile()`: Standard isolation scoping filesystem writes strictly to the project directory.
  - `apply_internal_profile()`: Lightweight execution for low-privilege internal tools.
  - `apply_build_profile()`: Grants explicit read/write access to compiler caches (`ccache`, `meson`, `pip`), system include directories, and project build trees while masking user documents.
  - `apply_strict_agent_profile()`: Maximum isolation for arbitrary LLM shell tool executions; masks `$HOME`, isolates `/tmp`, disables network access by default, and sets up a clean environment.
- **Execution Engine**:
  - Encapsulates subprocess lifecycle via POSIX `pipe()` and `poll()`.
  - Non-blocking I/O loop: `poll()` prevents indefinite hangs if a child process stops reading or writing.
  - Configurable timeouts (`set_timeout()`) with automatic SIGKILL escalation on timeout expiration.
- **Injection Defense**:
  - Uses templated formatted execution (`execute(fmt, args...)`) that automatically runs all arguments through `fs_utils::escape_shell_arg` before shell interpolation.
- **Crash Detection**:
  - Integrates with `libturbocatch.so` via `set_enable_crash_catcher(true)` and crash cookie tracking, allowing post-mortem inspection of failed child processes in `crashdump_manager`.

## Lessons Learned
- **Never Block on Raw I/O Pipes**: Using unbuffered `fgets()` or `read()` directly on subprocess pipes can deadlock the caller if the child hangs or blocks on interactive stdin. Driving execution through `poll()` with timeout loops guarantees responsiveness and cancellation support.
- **Package Manager Caches**: Strict sandboxing breaks compilers and package managers if user cache paths (`~/.cache/ccache`, `~/.cache/pip`) are hidden, triggering severe performance regressions or build failures. Dedicated cache allowlists (`add_cache_rw_exceptions()`) resolve this without compromising security.
- **Exit Code Fidelity**: Distinguishing true process exit codes from signal kills (`WIFSIGNALED` vs `WIFEXITED`) is critical for error diagnostic parsers and crashdump backtrace triggers.
