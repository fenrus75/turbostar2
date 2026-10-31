# fs_glob Tool

## Overview
The `fs_glob` tool matches files and directories matching a glob pattern (e.g. `src/**/*.cpp` or `include://*.h`), returning relative paths, file types, and line counts. It supports standard filesystem traversal as well as VFS (Virtual File System) lookups.

## Goals & Constraints
- Validate and confine all filesystem search paths within the current project root or sandbox boundaries.
- Limit output results (default cap at 100 entries) to prevent exhausting LLM prompt context windows.
- Convert glob wildcards (`*`, `?`, `**`) to regular expressions cleanly using PCRE2 (`pcre2_regex`).
- Ignore non-searchable or build-heavy directories (e.g. `.git`, cache folders) where appropriate.

## Lessons Learned
- When initializing aggregate structs (`fs_glob_args`) using designated initializers in C++20/C++23 under GCC's `-Wmissing-field-initializers` and `-Wextra`, provide explicit default member initializers for all members and explicitly specify fields to prevent compiler warnings.
- Migrated glob regex matching from RE2 to `pcre2_regex` to eliminate transitive Abseil library dependencies.
