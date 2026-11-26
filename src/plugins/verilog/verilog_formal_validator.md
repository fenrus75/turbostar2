# `tools::verilog_formal_validator`

Validator and tool schema registration for `verilog_formal` (SymbiYosys formal verification).

## Goals & Constraints
- Validates `module`, `mode` (`bmc`, `prove`, `cover`, `all`), `depth` (`1..500`), `solver` (`auto`, `bitwuzla`, `yices`, `z3`), and `timeout_sec` (`5..600`).
- Belongs to the `verilog` tool family so it adds zero schema tokens to non-FPGA projects.

## Lessons Learned
- Keeping `sby` in a dedicated `verilog_formal` tool avoids semantic collisions with `verilog_simulate` parameters (`testbench`, `level`, `coverage`, `include_formal`).
