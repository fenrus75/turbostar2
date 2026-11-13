# turbostar-verilog-timing

`turbostar-verilog-timing` (with a backwards-compatible `turbostar-verilog-update-timing` symlink) is a standalone 3-in-1 CLI utility that exposes all three Verilog/SystemVerilog timing analysis workflows directly in the developer terminal, Makefiles, and CI pipelines:
1. **Logic Depth Mode (default)**: Runs Yosys synthesis + `netlist_graph_analyzer` for a single module (`-m <module>`, positional module/file, or `top_module` from `turbostar.json`) and prints Input/Output logic layer tables, Top-$K$ critical combinational paths with inline RTL code (`-c, --show-code`), and the Area Summary.
2. **Physical Place & Route Mode (`--pnr`)**: Runs `nextpnr_timing_analyzer` (on-demand Yosys + multi-seed `nextpnr` or pre-generated `--timing-report` + `-n/--netlist`) and prints $F_{\max}$, slack, critical path segments (`--segments` / `-v`), logic vs. wire delay breakdown, tile Manhattan distance `(dx, dy)`, and inline RTL code snippets. Supports out-of-context submodule PnR (`--pnr -m <submodule>`) and multi-seed parallel exploration (`--seeds <N>`).
3. **Batch Update Mode (`-u, --update` or `-d, --dry-run`)**: Scans workspace Verilog/SystemVerilog files, synthesizes all discovered modules, and writes `<module>.timing.md` sidecars (or previews them in `--dry-run` mode).

## Goals & Constraints

- Enable developers, continuous integration, and Makefiles to run interactive logic-depth analysis, physical PnR timing closure, or batch sidecar updates without running the TurboStar TUI or MCP server.
- Formats terminal screen tables using `markdown_utils::align_all_tables` with UTF-8 box-drawing frames by default (disable via `--no-frames`), while keeping `.timing.md` sidecar files on disk as unframed standard Markdown tables.
- Supports target architectures: `Generic LUT4` (default), `Generic LUT6`, `iCE40`, `ECP5`, `Gowin`, `Xilinx`, and `Generic Gates`.
- Strictly adheres to untrusted parameter handling and secure canonical path resolution.

## Lessons Learned

- **Decoupling from Agent Environment**: By factoring out module discovery into `verilog_module_scanner` and keeping `netlist_graph_analyzer` and `nextpnr_timing_analyzer` independent of the MCP server runtime, all 3 timing analysis engines are shared seamlessly between agent MCP tools and the human CLI.
- **Terminal vs. Sidecar Table Formatting**: `markdown_utils::formatted_display_width` discounts Markdown formatting markers (`` ` `` and `**`) intended for the TUI renderer. Stripping inline backticks and bold markers inside table rows before calling `markdown_utils::align_all_tables(..., true)` ensures exact column alignment in UTF-8 box-drawing tables printed to standard output, while leaving disk `.timing.md` sidecar files untouched.
- **Project Configuration Integration**: When executed without explicit `-p`, `-m`, or `-t` arguments, `turbostar-verilog-timing` automatically inspects `turbostar.json` via `fpga_project_settings`, defaulting to the project's `sv_path`, `top_module`, and FPGA architecture target.
