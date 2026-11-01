# vcd_signal_to_code_validator

## Goals & Constraints
- Validates parameters for `vcd_signal_to_code`.
- Enforces strict security sandbox checks on any provided `netlist` path.
- Resolves untrusted input parameters into clean, safe types adhering to the untrusted data policy.
- Integrates with the `verilog` plugin tool registry.

## Lessons Learned
- `netlist` should remain optional so agents can query signals directly if standard workspace netlists (`netlist.json`, `build/netlist.json`) already exist without needing to guess paths.
