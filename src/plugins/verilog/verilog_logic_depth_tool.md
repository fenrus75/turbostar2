# verilog_logic_depth_tool

## Goals & Constraints
- Analyzes the structural layers of logic (LUT/gate depth) of a Verilog/SystemVerilog module to evaluate timing feasibility pre-place-and-route.
- Reports `In -> Reg`, `Reg -> Out`, `Reg -> Reg`, and `In -> Out` logic depths along with the top worst critical paths mapped to source RTL coordinates.
- Supports both pre-existing Yosys JSON netlists and on-the-fly synthesis via Yosys.
- Purely read-only and side-effect free, leaving sidecar file creation to the dedicated `verilog_update_timing` tool and `turbostar-verilog-update-timing` CLI.

## Lessons Learned
- Sidecar file generation is handled by the dedicated `verilog_update_timing` tool, keeping `verilog_logic_depth` purely read-only and safe for read-only subagents.
- Providing both a table summary and a step-by-step hop breakdown allows the LLM to understand both the high-level interface contract and the exact line of SystemVerilog causing a long combinational chain.
- If `safe_file` is supplied without an explicit module name, the tool derives the target from the file or auto-detects the top module in the synthesized AST.
- **Bottleneck Grouping by Sink**: In wide datapath registers (e.g. 16-bit PC counters, 32-bit ALU operands), separate source bits all converge on the same endpoint register through identical logic chains. Reporting separate paths per source bit crowded out genuinely distinct bottlenecks. Defaulting `group_by_sink: true` collapses these converging paths into a single bottleneck entry listing all converging source signals and bit ranges (`[0..15]`), while allowing callers to request raw paths via `group_by_sink: false`.
