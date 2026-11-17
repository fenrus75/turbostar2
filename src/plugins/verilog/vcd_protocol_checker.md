# `vcd_protocol_checker`

Abstract plugin interface and report types for post-simulation VCD bus protocol validation.

## Goals & Constraints
- **Zero False Positives**: Implementations must only bind `protocol_interface` instances on exact, unambiguous signal-bundle matches within a VCD scope.
- **Explicit Binding Ready**: `protocol_interface` carries `(scope, prefix, clock_signal, reset_signal, signals)` so future `turbostar.json` overrides can bypass auto-discovery without changing checker logic.

## Lessons Learned
- Initial design: separate `discover_interfaces` from `check` so `vcd_file_info`, `vcd_compare`, and `verilog_simulate` can inspect discovered bus topologies and evaluate rules uniformly.
