# `verilog::verilog_coverage_report`

Parses and merges Verilator `--coverage-line --coverage-toggle` `.dat` files across multiple testbenches into per-module RTL coverage summaries and detailed per-line gap reports served via `system://verilog/coverage.md`.

## Goals & Constraints
- **RTL-Only Filtering**: Restricts coverage points strictly to project RTL files (excluding `tb/` testbench files).
- **Direct `.dat` Parsing**: Parses Verilator `C '\x01...\x02...' <count>` records in-process so merging across many testbenches requires zero extra subprocesses.
- **High-Signal Port Toggle Summary**: Cross-references toggle records against declared module ports (`verilog_module_scanner::extract_module_ports`) to highlight stuck-at-0 / stuck-at-1 ports without internal-net noise.
- **Session Deltas Without Dirtying Config**: Persists previous per-module percentages in the project tmp directory (`verilog_coverage_last.json`) rather than `turbostar.json`.

## Lessons Learned
- Verilator 5.048 encodes the module name in the `page` field (`v_line/<mod>` / `v_toggle/<mod>`) and the full block line span in the `S` field (`132-139,141`), making per-module attribution exact without running `verilator_coverage`.
