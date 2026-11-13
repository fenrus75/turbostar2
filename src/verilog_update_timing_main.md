# turbostar-verilog

`turbostar-verilog` is the unified standalone SystemVerilog, FPGA & VCD CLI utility that exposes the entire `verilog` plugin toolset directly in the developer terminal, Makefiles, and CI pipelines:
1. **`config`**: Inspects or updates FPGA hardware settings and testbench associations in `turbostar.json` (`verilog_project_settings`, `verilog_configure_testbench`).
2. **`timing` (or default top-level mode)**:
   - **Logic Depth Mode (default)**: Runs Yosys synthesis + `netlist_graph_analyzer` for a module (`-m <module>`) and prints Input/Output logic layer tables, Top-$K$ critical combinational paths with inline RTL code (`-c, --show-code`), and the Area Summary.
   - **Physical Place & Route Mode (`--pnr`)**: Runs `nextpnr_timing_analyzer` (on-demand Yosys + multi-seed `nextpnr` or pre-generated `--timing-report`) and prints $F_{\max}$, slack, critical path segments (`--segments`), logic vs. wire delay breakdown, tile Manhattan distance `(dx, dy)`, and inline RTL code snippets.
   - **Batch Update Mode (`-u, --update` or `-d, --dry-run`)**: Scans workspace Verilog/SystemVerilog files, synthesizes all discovered modules, and updates in-place `// [timing: ...]` port annotations in `.sv`/`.v` source files.
3. **`compile` & `sim`**: Hierarchy-scoped Verilator lint + Yosys synthesis + `nextpnr` triage (`verilog_compile`) and parallel tiered testbench simulation (`verilog_simulate`).
4. **`vcd-info`, `vcd-wave`, `vcd-search`, `vcd-compare`, `vcd-trace`, `vcd-labels`, `vcd-qualifier`**: Clock-aligned VCD waveform tables, synchronous condition search, cycle-accurate VCD trace diffing, RTL driver/load traceability, and symbolic enum/value labels and validity qualifiers.

## Goals & Constraints

- Enable developers, continuous integration, and Makefiles to configure `turbostar.json`, run RTL compilation/simulation, inspect VCD waveforms, or perform logic-depth and physical PnR timing closure without running the TurboStar TUI or MCP server.
- Links against `libverilog_core` so all tool validators and engines are shared without duplicate compilation across `verilog.so`, `turbostar-verilog`, and unit test binaries.
- Formats terminal screen tables using `markdown_utils::align_all_tables` with UTF-8 box-drawing frames by default (disable via `--no-frames`), and vertically aligns numbered critical path and segment chains for human readability.
- Strictly adheres to untrusted parameter handling and secure canonical path resolution.

## Lessons Learned

- **Shared Static Core (`libverilog_core`)**: Compiling the Verilog/VCD engines and validators once into `libverilog_core` allows both the dynamically loaded `verilog` plugin (`link_whole: libverilog_core`) and the standalone `turbostar-verilog` CLI to share every tool implementation with zero code duplication.
- **Terminal Table & Chain Formatting**: `markdown_utils::formatted_display_width` discounts Markdown formatting markers (`` ` `` and `**`) intended for the TUI renderer. Stripping inline backticks and bold markers inside table rows before calling `markdown_utils::align_all_tables(..., true)` ensures exact column alignment in UTF-8 box-drawing tables printed to standard output.
- **Project Configuration Integration**: When executed without explicit `-p`, `-m`, or `-t` arguments, `turbostar-verilog` automatically inspects `turbostar.json` via `fpga_project_settings`, defaulting to the project's `sv_path`, `top_module`, and FPGA architecture target.
