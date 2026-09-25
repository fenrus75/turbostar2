# Crashdump Clear (`crashdump_clear`)

## Goals
The `crashdump_clear` tool allows an AI agent to clean up crash dump artifacts from the disk and reset the internal crash dump list in `crashdump_manager`. It supports clearing either a specific crash dump by `crash_id` or all crash dumps at once.

## Architecture and Constraints
- **Impure Tool (Domain 3 - System/Editor State)**:
  - Directly removes directories and files under the project dump directory.
- **Optional `crash_id` Targeting**:
  - When `crash_id` is omitted, it clears the entire crash dump repository via `crashdump_manager::clear_all()`.
  - When `crash_id` (or alias `id`) is provided, it specifically deletes only that dump via `crashdump_manager::clear_crash(crash_id)`.
- **Thread Safety**:
  - Mutex protection inside `crashdump_manager` guarantees synchronization between parallel test runs or agent tasks.

## Lessons Learned
- **Crash ID Argument Tolerance**: When an agent investigates a specific crash ID reported by a test runner or crash notification, it naturally attempts to clear or acknowledge that specific crash ID. Failing validation on unexpected arguments created friction and broke automated debugging loops. Allowing an optional `crash_id` with an alias for `id` makes the workflow intuitive and resilient.
