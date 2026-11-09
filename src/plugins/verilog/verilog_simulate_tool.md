# Verilog Simulate Tool (`verilog_simulate_tool`)

## Goals & Responsibilities
Compiles and executes a SystemVerilog/Verilog testbench together with the project's synthesizable RTL using Verilator (`--binary --timing --trace`), generates or updates the simulation waveform (`.vcd`), pre-warms `vcd_parser_cache`, and returns simulation output along with a VCD summary and concrete next-step `vcd_file_info` / `vcd_wave` commands.

Key capabilities:
1. **Zero-Config Testbench & RTL Discovery**: Automatically locates testbench files (`tb/`, `testbench/`, `tb_*.sv`, `*_tb.sv`), extracts the top-level testbench module name, and combines it with all synthesizable RTL under `sv_path`.
2. **Automatic `$readmemh` / `$readmemb` Asset Staging**: Scans source files for relative `$readmemh`/`$readmemb` initialization filenames (e.g. `ram_init.hex`) and stages symlinks in the simulation working directory so memory initialization succeeds without manual Makefile wrapper steps.
3. **LRU Cache Warming & Actionable Guidance**: Immediately parses the generated `.vcd` into `vcd_parser_cache` and emits copy-pasteable `vcd_file_info(...)` and `vcd_wave(...)` tool calls tailored to the discovered hierarchy and signals.

## Lessons Learned
- Running Verilator without `-flto` and placing `--Mdir` (resolved via `std::filesystem::absolute`) in the project temporary directory avoids polluting the repository with `obj_dir/` while keeping incremental C++ rebuilds fast.
- Passing `CCACHE_DISABLE=1 OBJCACHE=` to `verilator` prevents Verilator's generated Makefile from failing when `~/.cache/ccache` is mounted read-only inside the subprocess execution sandbox.
- Warming `vcd_parser_cache` at the end of `verilog_simulate` eliminates redundant VCD parsing on the immediately following `vcd_file_info` or `vcd_wave` call.
