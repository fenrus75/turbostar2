# netlist_graph_analyzer

## Goals & Constraints
- Serves as the central, reusable RTL graph and timing depth analysis engine for Verilog/SystemVerilog modules.
- Parses elaborated and technology-mapped Yosys JSON netlists (`write_json netlist.json`).
- Categorizes circuit elements into timing boundaries:
  - Sequential elements (`$_DFF_*`, `$dff`, `$adff`, `SB_DFF*`, `FD*`): Clock pins, Sinks (`D`, `EN`, `R`, `S`), Sources (`Q`).
  - Primary module ports: Inputs (Sources/Clock), Outputs (Sinks).
  - Combinational logic (`$add`, `$mux`, `LUT4`, `LUT6`, primitive gates): Propagate depth (+1 per logic layer).
- Computes structural logic layer metrics:
  - Input to Register (`In -> Reg`)
  - Register to Output (`Reg -> Out`)
  - Internal Register to Register (`Reg -> Reg`)
  - Combinational feedthrough (`In -> Out`)
- Traverses the Directed Acyclic Graph (DAG) using memoized dynamic programming with cycle detection to prevent infinite recursion on unexpected combinational loops.
- Formats token-efficient sidecar Markdown documents (`<module>.timing.md`) with two clear tables: Inputs (`Name | Width | Layers to | Sinks to`) and Outputs (`Name | Width | Layers from | Driven from`).
- Extracts detailed critical paths with source RTL coordinates (`file.sv:line`) for module optimization.

## Lessons Learned
- In Yosys JSON netlists, port connections can reference constant strings (`"0"`, `"1"`, `"x"`, `"z"`) in addition to integer bit IDs; the bit parser must treat non-integer tokens safely as constant drivers/sinks.
- Multi-bit ports often have individual bits with varying logic depths. Timing analysis must report the worst-case (maximum) depth across all bits of a port to reflect setup/hold constraints.
- DFF clock inputs (`CLK`, `C`) must be distinguished from sequential data/enable/reset sinks so that primary clock ports are classified as `Clock` rather than `Register`.
- Yosys maps synchronous reset DFFs and latches to gate-level primitives such as `$_SDFF_PP0_`, `$_DFFE_PP_`, etc. during `techmap`/`simplemap`. Sequential cell type detection must recognize all `$_*FF*` and `$_*LATCH*` variants to avoid misinterpreting flip-flops as combinational logic.
- Under systemd-based sandboxed command execution, `/tmp` is isolated via `PrivateTmp=true`. Any temporary netlist files produced during on-the-fly synthesis must reside inside the sandbox-accessible project directory rather than `/tmp`.
- For composite modules instantiating submodules, on-the-fly synthesis must invoke Yosys `flatten` (e.g. `hierarchy -check; proc; opt; flatten; opt; ...`) so that hierarchical submodules are flattened into primitive logic gates and flip-flops. Without flattening, submodules remain unelaborated black boxes that can be misclassified as combinational logic cones and mask internal sequential boundaries.
- Upstream traversal directly from primary output ports backwards to their driving sources (`Register` or `Input`) avoids downstream fanout collisions, ensuring every output port is traced to its true source driver and logic depth rather than being overwritten by other fanout branches and erroneously reported as "Constant".
- Module declaration scanners must strip multi-line block comments (`/* ... */`) and line comments prior to keyword matching to avoid matching module keywords inside code comments (e.g., `/* todo: make this a module so the top level is only modules and wires */`).
- Editor backup files (such as `*~`, `*.bak`, `*.org`, `*.twoport`, `.#*`) must be strictly excluded during directory scanning to prevent obsolete or truncated code from polluting synthesis runs.
