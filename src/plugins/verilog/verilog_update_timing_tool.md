# verilog_update_timing_tool

## Goals & Constraints
- Automatically scans project directories for Verilog/SystemVerilog files (`.sv`, `.v`), respecting `sv_path` configured in `turbostar.json`.
- Extracts declared module names and computes logic depth metrics using `netlist_graph_analyzer`, defaulting target architecture from project settings (`cfg.target`).
- Writes or updates `<module>.timing.md` sidecar documents directly adjacent to their source definitions in batch.
- Emits a clean Markdown progress summary showing module counts, depth numbers, and status.

## Lessons Learned
- Stripping single-line comments before regex module extraction prevents false positives from commented-out code or architectural notes.
