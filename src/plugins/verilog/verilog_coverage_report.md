# `verilog::verilog_coverage_report`

Parses and merges Verilator `--coverage-line --coverage-toggle` `.dat` files across multiple testbenches into per-module RTL coverage summaries and detailed per-line gap reports served via `system://verilog/coverage.md`.

## Goals & Constraints
- **RTL-Only Filtering**: Restricts coverage points strictly to project RTL files (excluding `tb/` testbench files).
- **Direct `.dat` Parsing**: Parses Verilator `C '\x01...\x02...' <count>` records in-process so merging across many testbenches requires zero extra subprocesses.
- **High-Signal Port Toggle Summary**: Cross-references toggle records against declared module ports (`verilog_module_scanner::extract_module_ports`) to highlight stuck-at-0 / stuck-at-1 ports without internal-net noise.
- **Session Deltas Without Dirtying Config**: Persists previous per-module percentages in the project tmp directory (`verilog_coverage_last.json`) rather than `turbostar.json`.

## Lessons Learned
- Verilator 5.048 emits `t=line`/`t=toggle` and `:0->1`/`:1->0` toggle suffixes, whereas Verilator 5.032 (Debian) omits the `t` field altogether (encoding type only in `page=v_line/<mod>` / `page=v_toggle/<mod>`) and emits combined toggle counts without `:0->1`/`:1->0`. Supporting both ensures compatibility across Verilator 5.03x and 5.04x.
- Never report `0/0` instrumented lines as `100.0%` ("All instrumented lines covered"); report `N/A (0/0)` with a warning so missing coverage data is immediately visible.
