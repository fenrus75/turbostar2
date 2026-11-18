# `verilog_project_map_validator`

Validates arguments for `verilog_project_map` and registers it in the `verilog` tool family as a pure (read-only) tool.

## Goals & Constraints
- **Pure Classification**: `is_pure()` returns `true` so read-only/research agents can map RTL projects safely.
- **Zero-Arg Entry**: Both `module` and `max_depth` are optional so `verilog_project_map()` works immediately on any project.

## Lessons Learned
- Initial implementation: clamps `max_depth` to `[1, 64]` to prevent excessive recursion depth.
