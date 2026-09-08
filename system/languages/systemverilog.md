# SystemVerilog Guidelines (IEEE 1800)

## Core Principles
- **Modeling Precision**: Use `logic` for general design signals rather than legacy `reg` or `wire`. Reserve `wire` strictly for structural nets with multiple drivers (e.g., tri-state buses).
- **Explicit Procedural Blocks**:
  - `always_comb` for pure combinational logic (enforces sensitivity list inference and zero-delay simulation semantics).
  - `always_ff @(posedge clk or negedge rst_n)` for sequential registers and flip-flops.
  - `always_latch` when level-sensitive latches are deliberately intended.
- **Non-Blocking vs. Blocking Assignments**:
  - Use non-blocking assignments (`<=`) inside `always_ff` blocks.
  - Use blocking assignments (`=`) inside `always_comb` blocks.
  - Never mix blocking and non-blocking assignments to the same signal.
- **Interfaces & Modports**: Bundle inter-module signals into `interface` definitions with dedicated `modport` views for master/slave roles.
- **Packages**: Encapsulate shared parameters, types, `typedef struct packed`, and enumeration definitions inside named `package` declarations and import them explicitly (`import my_pkg::*;`).
- **SystemVerilog Assertions (SVA)**:
  - Embed immediate (`assert (cond) else $error(...)`) and concurrent (`assert property (...)`) checks directly in RTL.
- **Synthesis & Linting**:
  - Keep RTL synthesizable; avoid delays (`#`), unsynthesizable initial blocks, and non-constant division.
  - Check code using `verilator --lint-only -Wall` or similar linter.
