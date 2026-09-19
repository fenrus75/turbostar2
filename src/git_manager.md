# Git Manager (`git_manager`)

## Goals
The `git_manager` class provides asynchronous monitoring and interaction with Git version control for open project workspaces. It tracks repository status, active branch names, and modified file states without blocking the editor UI thread during typing or navigation.

## Architecture and Constraints
- **Singleton Lifecycle**: Accessed via `git_manager::get_instance()`. Started during editor setup with a reference to the main `event_queue`.
- **Decoupled Worker Architecture**:
  - Contains an internal worker thread (`worker_loop`) driven by a synchronized FIFO request queue (`requests_`) and `std::condition_variable cv_`.
  - Non-blocking requests (`request_status`, `git_add`) return immediately to the caller and execute git subcommands asynchronously.
- **Cache Synchronization**:
  - Cached branch and status records (`cached_info_`) are protected by `std::mutex mutex_`.
  - When a status change is detected, the worker pushes an `event_type::git_status_update` to `event_queue`, prompting the editor to update window titles and UI status bars cleanly.

## Lessons Learned
- **UI Thread Stuttering**: Executing `git status` synchronously on keystrokes or buffer switches causes severe latency spikes on large repositories (e.g. the Linux kernel). Decoupling git operations to an asynchronous worker queue keeps editor frame rates silky smooth.
- **Repository Root Caching**: Determining the repository root via subprocess calls (`git rev-parse --show-toplevel`) on every query adds unnecessary overhead. Memoizing the root path upon repository initialization eliminates repetitive forks.
