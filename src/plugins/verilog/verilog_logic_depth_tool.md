# verilog_logic_depth_tool

## Goals & Constraints
- Analyzes the structural layers of logic (LUT/gate depth) of a Verilog/SystemVerilog module to evaluate timing feasibility pre-place-and-route.
- Reports `In -> Reg`, `Reg -> Out`, `Reg -> Reg`, and `In -> Out` logic depths along with the top worst critical paths mapped to source RTL coordinates.
- Supports both pre-existing Yosys JSON netlists and on-the-fly synthesis via Yosys.
- Optionally writes the compact `<module>.timing.md` sidecar file when `write_sidecar: true`.

## Lessons Learned
- Providing both a table summary and a step-by-step hop breakdown allows the LLM to understand both the high-level interface contract and the exact line of SystemVerilog causing a long combinational chain.
- If `safe_file` is supplied without an explicit module name, the tool derives the target from the file or auto-detects the top module in the synthesized AST.
