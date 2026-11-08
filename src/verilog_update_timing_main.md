# turbostar-verilog-update-timing

`turbostar-verilog-update-timing` is a standalone CLI utility that parses Verilog/SystemVerilog designs, discovers module declarations, invokes Yosys synthesis, and updates `<module>.timing.md` sidecars with timing bottleneck and logic depth tables.

## Goals & Constraints

- Enable continuous integration, Makefiles, and command-line workflows to regenerate timing sidecars without running TurboStar or the MCP server.
- Supports scanning a single file or a directory recursively.
- Supports target architectures: `Generic LUT4` (default), `Generic LUT6`, `iCE40`, `Generic Gates`.
- Supports pre-synthesized JSON netlists via `--netlist` to avoid re-running synthesis.
- Supports `--dry-run` to inspect logic depth and pin counts without modifying disk files.
- Return exit code `0` on complete success, non-zero if any module fails to synthesize or write.
- Strictly adheres to untrusted parameter handling and secure canonical path resolution.

## Lessons Learned

- **Decoupling from Agent Environment**: By factoring out module discovery into `verilog_module_scanner` and keeping `netlist_graph_analyzer` pure C++ standard library, the timing analyzer is shared seamlessly between MCP tools and build pipelines.
- **Dry-run Mode**: Having `--dry-run` allows users and scripts to quickly verify timing numbers in stdout without dirtying git working trees.
- **Project Configuration Integration**: When executed without explicit `-p` or `-t` arguments, `turbostar-verilog-update-timing` automatically inspects `turbostar.json` via `fpga_project_settings`. If configured, it defaults to the project's `sv_path` directory (skipping testbenches) and architecture target (e.g. `iCE40`), providing complete consistency between the standalone CLI utility and LLM agent tool calls.
