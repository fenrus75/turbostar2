# Plan Mode Workflow Guidelines

## Guidelines
- **Mandatory for Complex Tasks**: For complex multi-file tasks or non-trivial refactoring, call `enter_plan_mode` before making edits.
- **Read-Only Exploration**: While in plan mode, only read-only tools (and scratch `tmp://` writes) are available.
- **Plan Presentation**: Formulate a clear, step-by-step plan, present it to the user, and call `exit_plan_mode` to begin implementation.
