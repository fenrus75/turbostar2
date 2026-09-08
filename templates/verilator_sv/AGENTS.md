# @@PROJECT_NAME@@ Development Guidelines

## Build & Test Instructions
- **Lint**: `make lint` or `verilator --lint-only -Wall -sv src/top.sv`
- **Build Simulation**: `make`
- **Run Testbench**: `make test`

## Code Conventions
- Language Standard: @@LANGUAGE_STD@@
- Use `logic` for all nets and variables instead of `wire`/`reg`.
- Use `always_comb` for combinational logic and `always_ff @(posedge clk or negedge rst_n)` for flip-flops.
- Always separate module interfaces into clean input/output signal definitions.
