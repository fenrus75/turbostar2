# verilog_update_timing_tool

## Goals & Constraints
- Automatically scans project directories for Verilog/SystemVerilog files (`.sv`, `.v`), respecting `sv_path` configured in `turbostar.json`.
- Extracts declared module names and computes logic depth metrics using `netlist_graph_analyzer`, defaulting target architecture from project settings (`cfg.target`).
- Annotates module port declarations in-place inside `.sv`/`.v` source files with vertically aligned `// [timing: ...]` comments (e.g., `// [timing: 0 -> Reg]`, `// [timing: 13 <- Reg]`), preserving user comments.
- Skips modifying source files for any module whose synthesis fails, and emits a Markdown summary table showing per-module Reg->Reg depth, LUT/FF area, port counts, and status.
## Lessons Learned
- Stripping single-line and block comments before regex module extraction prevents false positives from commented-out code or architectural notes.
- Writing timing contracts directly on `.sv` port lines (`// [timing: ...]`) rather than separate `.timing.md` sidecar files ensures coding agents see input/output logic depths immediately whenever reading a module interface.
