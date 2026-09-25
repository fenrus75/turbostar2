# Git Log Tool (`git_log`)

## Goals
The `git_log` tool retrieves recent commit history formatted as a concise, one-line-per-commit log (`git --no-pager log -n <limit> --oneline --no-color`). It allows autonomous agents and users to view repository history without resorting to raw shell commands, and supports scoping log output to specific files or directories.

## Architecture and Constraints
- **Pure Tool**: Marked as `is_pure() == true`, allowing safe invocation by read-only subagents.
- **Parameters**:
  - `commit_id`: Optional specific commit hash or revision (e.g. `HEAD`, `71a56077`, `main`) to inspect. When provided, invokes `git show` for that specific commit.
  - `limit`: Optional maximum number of commits to retrieve (default: 10, clamped between 1 and 1000).
  - `path`: Optional relative path under the project workspace or VFS URI (e.g. `src/main.cpp`). If omitted or `.`, retrieves commits across the whole repository.
  - `show_patch`: Optional boolean. If true, includes the unified diff (patch) for commits.
  - `stat`: Optional boolean. If true, includes diffstat file changes summary.
- **Global & Custom Parameter Aliases**:
  - `commit`, `revision`, `ref`, `hash` -> `commit_id`.
  - `patch`, `diff`, `show_diff`, `p` -> `show_patch`.
  - `show_stat` -> `stat`.
  - `file_path`, `filepath`, `filename`, `file`, `target_file` -> `path`.
  - `count`, `max_results`, `max_count`, `n` -> `limit`.
- **Security & Sandboxing**:
  - Validates `path` using `ctx.fs_security.validate_access(untrusted_path, agentlib::access_type::read, safe_path, out_error)`.
  - Validates `commit_id` character whitelist to prevent command injection.
  - Escapes `safe_path` and `commit_id` using `fs_utils::escape_shell_arg` when executing the git subprocess.
  - Enforces maximum output length limits (20,000 characters) to prevent context exhaustion.

## Lessons Learned
- **Path Scoping**: Autonomous agents frequently need to know the commit history of a single file they are editing. Providing a validated `path` parameter avoids invoking unconstrained shell commands while preserving security boundary checks.
- **Parameter Aliases**: Models frequently supply `file_path` or `filename` instead of `path`. Relying on central alias normalization in `tool_validator` keeps the tool implementation concise and robust against model naming discrepancies.
- **Commit Inspection and Patch Diffs**: Agents frequently need to inspect what changed in a specific commit or view patches (`git show` or `git log -p`). Providing native `commit_id`, `show_patch`, and `stat` support ensures that shell re-steering from `git show` / `git log -p` points to a fully functional native tool call rather than dead-ending or requiring security overrides.
