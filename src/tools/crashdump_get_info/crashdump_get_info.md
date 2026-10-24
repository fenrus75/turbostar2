# Crashdump Get Info (`crashdump_get_info`)

## Goals
The `crashdump_get_info` tool enables an AI agent to inspect detailed crash diagnostics, stack traces, failed assertion locations, signal numbers, and binary memory maps for a specific crash dump by `crash_id`. When `crash_id` is omitted, it defaults to the most recent crash chronologically.

## Architecture and Constraints
- **Pure Tool (Domain 1 - Read Only)**:
  - Only reads files and metadata from the crash dump directory; does not mutate system or editor state.
- **Optional `crash_id` Resolution**:
  - When `crash_id` is empty or omitted, selects the most recent crash based on directory modification time or highest numeric ID.
  - Automatically checks if an associated core file exists (either in the crash directory or via `systemd-coredumpctl`) and appends instructions for interactive GDB inspection via `agent_debug_coredump`.
- **String and Numeric Coercion**:
  - Accepts both string crash IDs (e.g. `"696982"`) and numeric integer crash IDs (e.g. `696982`) directly from JSON arguments, automatically coercing numeric IDs to strings during argument validation.
- **Safety**:
  - Validates `crash_id` length (max 128 characters) and ensures characters are alphanumeric plus `_` or `-` to prevent directory traversal or injection attacks.

## Lessons Learned
- **Type Coercion for Numeric Identifiers**: Test runners (such as `fs_run_tests` and `turbocatch`) report crash IDs as numbers (e.g., `Crash IDs: 696982`). When agents pass these numeric IDs directly to `crashdump_get_info(crash_id=696982)`, strict string schema validation previously rejected the call with a type mismatch error. Supporting numeric integers by coercing them to strings in `tool_validator` and `crashdump_get_info_security` eliminates unnecessary agent friction.
