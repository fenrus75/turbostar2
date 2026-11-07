# Git Restore Tool (`git_restore_tool`)

## Goals
`git_restore_tool` restores a tracked file in the repository back to its `HEAD` state via `git checkout -- <path>`.

## Constraints & Invariants
- Rejects virtual VFS paths and paths inside `.git`.
- Upon successful restore, invokes `project_manager::get_instance().notify_file_changed(safe_path_)` so all per-file caches and language servers observe the restored file contents.

## Lessons Learned
- **Avoiding Unconditional Byte-by-Byte File Reads**: Previously, `git_restore_tool` read the entire restored file into a heap string via `std::istreambuf_iterator<char>` before calling `lsp_save_document`, even for non-LSP files. Calling `project_manager::notify_file_changed(safe_path_)` invalidates all caches immediately and only reads the file via zero-copy `mapped_file::get_cached` when the file is supported by an active LSP backend.
