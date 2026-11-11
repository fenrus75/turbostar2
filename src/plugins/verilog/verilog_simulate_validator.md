# Verilog Simulate Validator (`verilog_simulate_validator`)

## Goals & Responsibilities
Validates parameters and enforces sandbox security rules for the `verilog_simulate` tool.

Key checks:
1. **Path Sandbox Validation**: Verifies any user-supplied `testbench` path against `ctx.fs_security.validate_access` (`access_type::read`) and `fs_utils::is_regular_file`.
2. **Module, Hierarchy & Level Validation**: Validates optional `module` (target RTL module name or source path), `hierarchical` (boolean, default `false`), and `level` (`quick`, `comprehensive`, or `extreme`, default `comprehensive`).
3. **Identifier Sanitization**: Ensures `top_module` is a valid Verilog identifier (`[a-zA-Z_][a-zA-Z0-9_$]*`) and safe for UI/shell usage.
4. **Timeout Clamping**: Bounds `timeout_sec` to a safe `[1, 300]` second execution window.

## Lessons Learned
- Providing parameter aliases (`file`, `tb`, `testbench_file` -> `testbench`, `top` -> `top_module`, `rtl_module` -> `module`, `tier` -> `level`) allows agents to invoke `verilog_simulate` naturally without schema friction.
- Separating `module` (the RTL module under test) from `top_module` (the testbench top-level wrapper module) enables module-targeted unit testing while preserving backward compatibility when `module` matches a testbench module name.
