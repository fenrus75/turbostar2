# Verilog PNR Timing Tool (`verilog_pnr_timing_tool`)

## Goals & Constraints

- **Purpose**: Implements the `verilog_pnr_timing` tool action, performing physical place-and-route timing analysis. Supports both pre-existing reports (`timing.json`) and automated on-demand synthesis + P&R execution.
- **Reg -> Reg Bottlenecks**: Focuses strictly on synchronous register-to-register paths determining $F_{\max}$, with clear delay attribution between logic depth and routing wire delay.
- **Graceful Guidance**: If target settings are missing, returns structured guidance explaining how the LLM can resolve the missing fields rather than failing abruptly.

## Lessons Learned

- **Filtering Testbenches**: When scanning workspace RTL for on-demand synthesis, automatically respecting `sv_path` and using `verilog_module_scanner::find_synthesizable_files` prevents top-level duplicate module collisions in Yosys from testbench files.
- **Out-of-Context Submodule PnR (`module`) & Seed Count (`seeds`)**: Accepts `module` (aliases `top_module`, `rtl_module`, `top`) and `seeds`. When `module` matches `cfg.top_module`, runs normal top-level PnR; when `module` targets a submodule, temporarily overrides `cfg.top_module` and clears top-level pin constraints (`cfg.pcf_file.clear()`) to run out-of-context unconstrained PnR with a clear note in the report.
- **Detailed Hop-by-Hop Critical Path Segments (`verbose` / `show_segments`)**: Forwards `args_.verbose` (`verbose=true` or `show_segments=true`) to `nextpnr_timing_analyzer::generate_timing_markdown` so users can inspect every cell type, pin transition, tile coordinate, and intermediate net along critical paths without manually parsing `pnr_report.json`.
- **Configurable RTL Code Snippets (`show_code`)**: Supports `show_code` (default `1`, aliases `code_lines`, `snippet_lines`, `include_code`, accepts integer line count or boolean `true`/`false`) to include 1-line inline RTL statements by default, multi-line context blocks when `show_code > 1`, or suppress snippets when `show_code = 0`/`false`.
