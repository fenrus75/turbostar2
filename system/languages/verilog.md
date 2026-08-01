# Verilog / SystemVerilog Guidelines

## Core Principles
- **Modeling**: Separate combinational (`always_comb` / `always @(*)`) logic from sequential (`always_ff` / `always @(posedge clk)`) logic.
- **Assignments**: Use non-blocking assignments (`<=`) in sequential blocks and blocking assignments (`=`) in combinational blocks.
- **Synthesis**: Avoid un-synthesizable constructs (delays `#`, initial blocks for logic) in RTL modules.
