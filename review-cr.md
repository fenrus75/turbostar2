# Review of command_runner (.h / .cpp)

## 1. Overview
The `command_runner` class acts as the core subprocess execution engine in Turbostar. It encapsulates security configurations, systemd-based sandboxing, output chunking, and integration with the crashdump manager. Additionally, it offers pre-defined profiles (`default`, `internal`, `build`, `strict`) to easily restrict permissions (network, filesystem, environments) depending on whether the runner executes internal utilities, build tasks, or AI agent tool executions.

---

## 2. Correctness & Security Issues

### 2.1 Critical Argument/Command Injection Vulnerability
In `command_runner::build_command`, the working directory (`project_dir_`), crashdump directory (`dump_dir`), and extra read-write/read-only paths are concatenated directly into the shell command string without escaping:
```cpp
if (!project_dir_.empty()) {
    cmd += "-p WorkingDirectory=" + project_dir_ + " ";
    if (home_access_ == home_access_t::hidden) {
        cmd += "-p BindPaths=" + project_dir_ + " ";
    } else {
        cmd += "-p ReadWritePaths=" + project_dir_ + " ";
    }
}
```
Because the final command is executed via `popen()`, which runs `/bin/sh -c`, any directory paths containing spaces or shell metacharacters (e.g., `;`, `&`, `|`, `$`) will result in unexpected argument splitting or arbitrary command execution under the user's privileges.
* **Risk Level:** **High / Critical**
* **Example Vector:** If a repository is cloned into `/home/user/project; rm -rf /`, the semicolon will terminate the `systemd-run` command and run `rm -rf /` immediately.

### 2.2 Blocking Cancellation via `pclose()`
The execution loop polls `should_continue()` to allow cancellation:
```cpp
while (should_continue()) {
    // poll and read loop...
}
int exit_code = pclose(pipe);
```
If `should_continue()` returns `false`, the loop exits, but the runner calls `pclose(pipe)`. According to POSIX, `pclose` blocks until the associated subprocess terminates. If the child process is hanging or runs indefinitely, `pclose` will block the calling thread (freezing the editor UI or agent loop) despite the cancellation request.
* **Risk Level:** **Medium**
* **Impact:** Cancellation is ineffective if the child process does not voluntarily exit upon standard pipe closure.

### 2.3 Non-Standard Include Placement
In `src/command_runner.cpp` line 231 (inside the `execute` method), there is a nested include statement:
```cpp
#include <unistd.h> // needed for read()
```
Including files within function bodies is a non-standard practice in C++ and can cause translation unit complications. Furthermore, `<unistd.h>` is already included at the top of the file (line 10).
* **Risk Level:** **Low (Style / Cleanliness)**

---

## 3. Thread-Safety & Exception Safety

### 3.1 Subclass State Safety
`command_runner` does not employ locks on its internal settings (`bypass_sandbox_`, `project_dir_`, etc.). If a runner instance is shared across threads, modifications to configurations during execution will cause race conditions. Fortunately, current uses appear to allocate runner instances locally or synchronously, but thread safety boundaries are not explicitly guarded.

### 3.2 Pipe Leakage on Exceptions
If `on_output_chunk` throws an exception, the execution loop is aborted, and `pclose(pipe)` is bypassed entirely. This results in resource leakage of the pipe file descriptor and leaves a zombie/orphaned child process.
* **Solution:** Clean up resources using a scope guard (RAII wrapper around the pipe `FILE*`) to guarantee `pclose()` is called regardless of how the function exits.

---

## 4. Performance & Efficiency

### 4.1 Fine-Grained Poll Timeout Delay
`poll()` uses a `100ms` timeout:
```cpp
int ret = poll(&pfd, 1, 100); // 100ms timeout
```
While this allows `should_continue()` to be checked 10 times a second, it introduces a small latency at the end of execution or when reading short outputs. When executing many quick, synchronous tasks (like `git status` check iterations), this timeout loop can accumulate unnecessary delays.

---

## 5. Code Design & Architecture

### 5.1 Redundant Repository Root Resolution
Both `command_runner::get_repository_root()` (in `src/command_runner.cpp`) and `git_manager::get_repository_root()` (in `src/git_manager.cpp`) contain identical duplicate code to run `git rev-parse --show-toplevel`. This should be unified into a single helper utility.

### 5.2 Systemd Coupling & Portability
The sandboxing mechanism is entirely hardcoded to `systemd-run`. If systemd is not present (e.g. inside docker containers, WSL without systemd support, macOS, or FreeBSD), any command executed through a sandboxed profile will fail to launch entirely.

### 5.3 Pure Virtual Abuse / Legacy Comments
The header lists `on_output_line` as a legacy fallback, suggesting the base class implementation of `on_output_chunk` accumulates lines:
```cpp
// By default, the base implementation of on_output_chunk accumulates lines and calls this.
virtual void on_output_line(const std::string& line) { (void)line; }
```
However, `on_output_chunk` is a pure virtual function (`= 0`) in the base class, meaning there is no base implementation. Subclasses must override `on_output_chunk` and manually duplicate the line buffering/splitting logic (as seen in `sync_command_runner`).

---

## 6. Refactor Opportunities

### 6.1 Eliminate Shell Invocation via Direct Execvp
Instead of calling `popen()` (which triggers `/bin/sh` and makes argument escaping error-prone), refactor the runner to spawn the process directly using `fork` and `execvp` (or `posix_spawn`).
* **Benefits:** Bypasses shell parsing entirely, removes argument injection vulnerabilities, and allows direct manipulation of process environment and file descriptors.

### 6.2 Implement RAII Pipe Management
Implement a unique resource wrapper for the pipe `FILE*` that invokes `pclose` or kills the child process group upon destruction.

### 6.3 Standardize Line Accumulator
Extract line splitting logic from `sync_command_runner::on_output_chunk` into a helper class or utility function to avoid duplication across other potential subclasses.

### 6.4 Non-Blocking Termination via Process Groups
Run subprocesses in their own process group (`setpgid`). If cancellation is requested, send `SIGKILL` to the process group (`kill(-pgid, SIGKILL)`) prior to closing descriptors and calling wait functions, preventing blocking hangs.
