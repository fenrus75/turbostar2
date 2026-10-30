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
- **Unified Diff Mapping (`git_file_diff`)**:
  - Executes `git diff -U0 HEAD -- <file>` to extract line-level hunk delta records (`git_diff_hunk`).
  - Maps any 1-based buffer line to its git status (`unchanged`, `added`, `modified`) and its corresponding git HEAD line number (`git_line`), plus deletion indicators (`has_deletion_above`, `deleted_count`).
- **In-Memory Diff Engine (`compute_diff`)**:
  - Leverages `<dtl/dtl.hpp>` to compute shortest edit scripts (SES) between cached HEAD lines and active buffer lines directly in memory.
  - Transforms SES operations (`SES_DELETE`, `SES_ADD`, `SES_COMMON`) directly into zero-context (`-U0`) hunks without intermediate string serialization or subprocess forks.

## Lessons Learned
- **UI Thread Stuttering**: Executing `git status` synchronously on keystrokes or buffer switches causes severe latency spikes on large repositories (e.g. the Linux kernel). Decoupling git operations to an asynchronous worker queue keeps editor frame rates silky smooth.
- **Repository Root Caching**: Determining the repository root via subprocess calls (`git rev-parse --show-toplevel`) on every query adds unnecessary overhead. Memoizing the root path upon repository initialization eliminates repetitive forks.
- **Single-Command Status and Branch Resolution**: Using `git status --porcelain=v1 -b -u --ignored=matching` delivers the current branch name, repo membership, and file status in a single subprocess call, eliminating redundant `git rev-parse --abbrev-ref HEAD` and `git rev-parse --is-inside-work-tree` invocations. For clean files, HEAD contents are read directly from disk, completely avoiding unnecessary `git show HEAD` forks.
- **Unified Diff Hunk Zero-Count Delta Accounting**: In unified diffs (`@@ -old_start,old_count +new_start,new_count @@`), pure deletions have `new_count == 0`. When calculating `get_line_info()`, lines with `buffer_line <= new_start` precede the deletion and must NOT have the deletion delta subtracted, while lines with `buffer_line == new_start + 1` indicate a deletion above the line.
- **In-Memory Live Diffing vs Disk Diffs**: Git CLI diffs compare disk files against HEAD. When a user is typing without saving, disk diffs remain stale. Caching HEAD lines via `git show HEAD:./file` in `git_info` and running in-memory diffs on the active document buffer allows live gutter indicator updates as the user types with sub-millisecond latency.
