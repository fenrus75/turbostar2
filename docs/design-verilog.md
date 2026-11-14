# Verilog & FPGA Hardware Design Architecture

The `verilog` plugin (`src/plugins/verilog/`) and standalone CLI (`turbostar-verilog`) provide an integrated hardware engineering suite for Verilog/SystemVerilog RTL linting, synthesis triage, testbench simulation, VCD waveform analysis, structural logic-depth analysis, in-place port timing annotations, and post-routing physical timing closure.
---

## 1. Project Configuration & Discovery (`fpga_project_settings`, `verilog_module_scanner`)

- **FPGA Project Settings (`fpga_project_settings`, `verilog_project_settings`)**:
  - Persists target hardware parameters (`target`, `device`, `package`, `top_module`, `freq_mhz`, `pcf_file`, `sv_path`, `yosys_synth_args`, and `testbenches`) in `turbostar.json` (or `project.json`).
  - Automatically scans for single `.pcf` physical pin constraint files, auto-detects synthesizable RTL source directories (`sv/`, `rtl/`, `src/`), and probes installed open-source EDA toolchains (`verilator`, `iverilog`, `yosys`, `nextpnr-ice40`, `nextpnr-ecp5`, `nextpnr-gowin`, `nextpnr-generic`).
  - Enables zero-argument structural and physical timing tool invocations by supplying default top modules, target architectures, and source directories.

- **Module, Hierarchy & Enum Scanner (`verilog_module_scanner`)**:
  - Performs fast comment-stripped and string-literal-aware scanning of `.sv` and `.v` files to extract declared module identifiers, filtering out testbench files (`tb_*`, `*_tb.sv`, `testbench/`, `tb/`) and editor backup artifacts.
  - Resolves module aliases (`resolve_module_name`) across filenames, `_mod` suffixes, and instance names.
  - Discovers module instantiation trees (`discover_instantiated_modules`, `discover_submodule_hierarchy`) to scope synthesis checks and execute bottom-up hierarchical testbench regressions.
  - Auto-discovers `typedef enum` and inline SystemVerilog `enum` constants (`discover_enum_signal_labels`) using `mapped_file` and `::memmem` pre-filtering to populate zero-config symbolic VCD waveform labels.

---

## 2. RTL Linting, Synthesis Triage & Simulation (`verilog_compile`, `verilog_simulate`)

- **Hierarchy-Scoped Compilation & Synthesis Triage (`verilog_compile_tool`)**:
  - Combines fast Verilator static linting (`lint_only=true`) with Yosys RTL synthesis and optional `nextpnr` place-and-route verification.
  - Automatically scopes source files to the requested `module`'s dependency hierarchy so unrelated modules or testbenches do not cause duplicate symbol errors.
  - Categorizes diagnostics into correctness/synthesis hazards (combinational loops, inferred latches, multi-driven nets) vs. style and unused-resource notes (`UNUSEDPARAM`, `UNUSEDSIGNAL`, `PINMISSING`) while saving full compiler output to `tmp://synthesis.log`.

- **Tiered & Parallel Simulation (`verilog_simulate_tool`, `verilog_configure_testbench_tool`)**:
  - Associates one or more testbenches with each RTL module in `turbostar.json` (with `hierarchical=true` support for bottom-up submodule + top-level regression suites) and supports one-command workspace auto-registration via `scan=true` / `turbostar-verilog config --scan`, which discovers all workspace testbenches, binds them to their instantiated DUT modules, and enables `hierarchical=true` on composite parent modules.
  - Executes multi-bench suites in parallel across CPU cores (`std::async`), using `iverilog`/`vvp` for fast single-bench compilation and switching to parallelized C++ `verilator --binary -j 0` when simulation runtime dominates.
  - Automatically stages `$readmemh` / `$readmemb` memory initialization files (`stage_readmem_files`) and auto-invokes cycle-accurate VCD differential comparison against `.golden.vcd` reference traces when present.

---

## 3. Simulation Waveform Inspection (`vcd_*`)

- **Streaming VCD Parser & Signal Analysis (`vcd_parser`, `vcd_label_manager`, `vcd_qualifier_manager`)**:
  - **Waveform Rendering (`vcd_wave`)**: Renders clock-aligned multi-bit bus and control waveforms over specified cycle or timestamp windows, supporting clock edge filtering (`posedge`/`negedge`) and hierarchical scope inspection (`vcd_file_info`).
  - **Semantic Value Labels (`vcd_signal_labels`)**: Substitutes raw hex/binary bus values with human-readable state and instruction names from `vcd_labels.json` and live-discovered SystemVerilog `enum` types.
  - **Valid/Strobe Qualifiers (`vcd_signal_qualifier`)**: Associates data buses with valid/enable qualifiers (`vcd_qualifiers.json`) to mask inactive clock cycles (`-`).
  - **Synchronous Condition Search & Trace Diffing (`vcd_search`, `vcd_compare`)**: Evaluates multi-signal boolean conditions (`==`, `!=`, `$rose`, `$fell`, `$stable`, `$changed`) synchronized to clock edges, and performs cycle-by-cycle differential comparisons between two VCD traces to pinpoint the exact cycle and signal of first divergence.
  - **RTL Driver/Load Traceability (`vcd_signal_to_code`)**: Maps VCD hierarchy nets back to their SystemVerilog declaration, driving blocks (`always_ff`, `always_comb`, `assign`), and downstream consumer loads.

---

## 4. Structural Logic Depth, Area & In-Place Port Annotations (`netlist_graph_analyzer`)

- **DAG Logic Depth & Area Engine (`netlist_graph_analyzer`, `verilog_logic_depth`)**:
  - Synthesizes target modules via Yosys (`flatten`, `memory -nomap`, and architecture-specific LUT/carry mapping with `abc -dress`) and parses the resulting JSON netlist.
  - Categorizes sequential boundaries (`$_DFF_*`, `SB_DFF*`, `$mem_v2`), primary ports, and combinational gates/LUTs/carry chains, computing worst-case structural depths across `In -> Reg`, `Reg -> Out`, `Reg -> Reg`, and `In -> Out (comb)`.
  - Groups redundant bit paths by destination bottleneck sink (`get_worst_bottlenecks()`), compacts source bit ranges (`pc[0..15]`), correlates intermediate gates to project-relative `file.sv:line` coordinates with inline RTL source snippets (`show_code`), and reports standalone module LUT/FF/Carry/RAM/DSP area summaries.

- **In-Place `.sv` Port Timing Annotations (`verilog_update_timing`)**:
  - Batch-synthesizes discovered modules across the workspace and annotates each module's port declaration lines directly in-place inside its `.sv`/`.v` source file (`update_verilog_file_annotations`) with vertically aligned `// [timing: ...]` comments:
    - Clock input: `// [timing: clock]`
    - Input to Register / Output: `// [timing: 0 -> Reg]`, `// [timing: 8 -> Reg]`, `// [timing: 4 -> Out (comb)]`
    - Output from Register / Input: `// [timing: 0 <- Reg]`, `// [timing: 13 <- Reg]`, `// [timing: 5 <- In (comb)]`
    - Special states: `// [timing: const]`, `// [timing: FLOATING]`, `// [timing: unused]`
  - Preserves existing user comments (`// ...` and `/* ... */`), aligns standalone timing comments using 8-column tab-stop visual width calculation, and operates idempotently across repeated runs. If synthesis fails for a module, its source file is left unmodified.

---

## 5. Physical Place-and-Route Timing Analysis (`nextpnr_timing_analyzer`, `verilog_pnr_timing`)

- **Post-Routing Timing Correlation (`nextpnr_timing_analyzer`, `yosys_netlist_reader`)**:
  - Runs on-demand Yosys synthesis + multi-seed `nextpnr` placement and routing (or loads pre-generated `timing.json` + `main.json` reports), supporting both full top-module PnR with `.pcf` constraints and out-of-context unconstrained submodule PnR (`module="alu8"`).
  - Reports achieved $F_{\max}$ (with multi-seed median and min-max range when adaptive multi-seed exploration runs), setup slack per clock domain, and hardware resource utilization.
  - Decomposes Top-$K$ critical `Reg -> Reg` paths into logic delay vs. routing wire delay, reporting net fanout (sink count), physical chip Manhattan tile span `(dx, dy)` (`nextpnr_Coordinate_parser`), project-relative `file.sv:line` anchors, inline RTL statements (`show_code`), and optional hop-by-hop cell/pin segment lists (`show_segments=true` / `--segments`).
  - Disambiguates registers in shared `always_ff` blocks and multi-instance submodules using Yosys wire driver connectivity (`D`/`Q`/`E` pin user nets and 1-hop upstream LUT input nets).

---

## 6. Standalone Unified CLI (`turbostar-verilog`)

- **Unified Terminal & CI Utility (`src/verilog_update_timing_main.cpp`, `libverilog_core`)**:
  - Packages the entire Verilog, FPGA timing, and VCD waveform toolset into a standalone binary (`turbostar-verilog`) usable in developer terminals, Makefiles, and CI pipelines without launching the TUI or MCP server:
    1. **Project & Testbench Configuration (`config`)**: `turbostar-verilog config [--target ice40 --device hx8k --package cb132 --top main --freq 80 --pcf pins.pcf --sv-path sv] [--scan] [--tb tb/tb_alu.sv]`
    2. **Timing Analysis (`timing` or default top-level mode)**:
       - Logic Depth Mode: `turbostar-verilog [timing] [-m module] [-k top_k] [-c lines]`
       - Physical Place & Route Mode: `turbostar-verilog [timing] --pnr [-m module] [--seeds N] [--segments]`
       - Batch In-Place Port Annotation Mode: `turbostar-verilog [timing] -u [--dry-run]`
    3. **Compilation & Simulation (`compile`, `sim`)**: `turbostar-verilog compile [-m mod] [--lint-only]` and `turbostar-verilog sim [-m mod] [-H]`
    4. **VCD Waveform Suite (`vcd-info`, `vcd-wave`, `vcd-search`, `vcd-compare`, `vcd-trace`, `vcd-labels`, `vcd-qualifier`)**: Clock-aligned waveform tables, multi-signal condition search, cycle-by-cycle VCD diffing, and RTL net traceability.
  - Formats terminal output with UTF-8 box-drawing tables via `markdown_utils::align_all_tables` (disable via `--no-frames`) and vertically aligns numbered critical-path and segment chains for human readability.
