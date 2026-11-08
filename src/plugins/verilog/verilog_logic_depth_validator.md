# verilog_logic_depth_validator

## Goals & Constraints
- Validates parameter inputs for the `verilog_logic_depth` tool.
- Enforces sandboxed file access validation (`ctx.fs_security.validate_access`) on both input files and pre-generated netlists.
- Registers and unregisters cleanly in `agentlib::tool_registry` under the `verilog` tool family.
- Dynamically classifies purity based on `write_sidecar` (`is_pure() == !args_.write_sidecar`).

## Lessons Learned
- When `write_sidecar` is false, the tool is pure (read-only analysis), enabling read-only subagents to run timing queries safely. When `write_sidecar` is true, it requires write permission to create the `.timing.md` file.
