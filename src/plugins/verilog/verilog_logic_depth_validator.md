# verilog_logic_depth_validator

## Goals & Constraints
- Validates parameter inputs for the `verilog_logic_depth` tool.
- Enforces sandboxed file access validation (`ctx.fs_security.validate_access`) on both input files and pre-generated netlists.
- Registers and unregisters cleanly in `agentlib::tool_registry` under the `verilog` tool family.
- Unconditionally pure (`is_pure() == true`), enabling read-only subagents to run timing queries safely.

## Lessons Learned
- Removing `write_sidecar` in favor of the dedicated `verilog_update_timing` tool makes `verilog_logic_depth` statically pure, simplifying agent permission rules and removing write side-effects from read-only workflows.
