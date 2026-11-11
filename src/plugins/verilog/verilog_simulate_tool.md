# Verilog Simulate Tool (`verilog_simulate_tool`)

## Goals & Responsibilities
Compiles and executes a SystemVerilog/Verilog testbench together with the project's synthesizable RTL using Verilator (`--binary --timing --trace`), generates or updates the simulation waveform (`.vcd`), pre-warms `vcd_parser_cache`, and returns simulation output along with a VCD summary and concrete next-step `vcd_file_info` / `vcd_wave` commands.

Key capabilities:
1. **Zero-Config Testbench & RTL Discovery**: Automatically locates testbench files (`tb/`, `testbench/`, `tb_*.sv`, `*_tb.sv`), extracts the top-level testbench module name, and combines it with all synthesizable RTL under `sv_path`.
2. **Module-Targeted & Hierarchical Testing (`module`, `hierarchical`, `level`)**: Runs all testbenches associated with a target RTL module (via `verilog_configure_testbench` in `turbostar.json` or auto-discovered by instantiation). When `hierarchical: true`, traverses the submodule tree bottom-up (leaf submodules first, target module last) and filters testbenches by cumulative `level` (`quick` $\subset$ `comprehensive` $\subset$ `extreme`).
3. **Waveform `$dumpfile` Collision Detection & Renaming**: Detects ahead of time if multiple testbenches write to the same `$dumpfile("...")` VCD path. Only when a collision exists, renames the generated waveform for colliding testbenches to `<tb_stem>.vcd` (along with `<tb_stem>.previous.vcd` and `<tb_stem>.lastpass.vcd`) and warns the agent to give each testbench a unique `$dumpfile` filename.
4. **Automatic `$readmemh` / `$readmemb` Asset Staging**: Scans source files for relative `$readmemh`/`$readmemb` initialization filenames (e.g. `ram_init.hex`) and stages symlinks in the simulation working directory so memory initialization succeeds without manual Makefile wrapper steps.
5. **Automatic Waveform History (`<stem>.previous.vcd` & `<stem>.lastpass.vcd`)**: Before executing the simulation binary, preserves any existing `.vcd` waveform as `<stem>.previous.vcd`. When the simulation testsuite passes (`PASS` / `PASSED` / `successfully` without errors), also snapshots `<stem>.lastpass.vcd` so multi-iteration debugging sessions never lose their last known-good reference trace.
6. **LRU Cache Warming & Actionable Guidance**: Immediately parses the generated `.vcd` into `vcd_parser_cache` and emits copy-pasteable `vcd_file_info(...)`, `vcd_wave(...)`, and `vcd_compare(...)` tool calls tailored to the discovered hierarchy, signals, and pass/fail status.

## Lessons Learned
- Running Verilator without `-flto` and placing `--Mdir` (resolved via `std::filesystem::absolute`) in the project temporary directory avoids polluting the repository with `obj_dir/` while keeping incremental C++ rebuilds fast.
- Passing `CCACHE_DISABLE=1 OBJCACHE=` to `verilator` prevents Verilator's generated Makefile from failing when `~/.cache/ccache` is mounted read-only inside the subprocess execution sandbox.
- Warming `vcd_parser_cache` at the end of `verilog_simulate` eliminates redundant VCD parsing on the immediately following `vcd_file_info` or `vcd_wave` call.
- Keeping both `<stem>.previous.vcd` and `<stem>.lastpass.vcd` prevents multi-step debugging loops from overwriting the last known-good waveform when two or more consecutive edits fail.
- Running `vcd_compare_tool::quick_compare` automatically at the end of `verilog_simulate` reports immediately when the new waveform is **IDENTICAL** to `<stem>.previous.vcd` (saving a `vcd_compare` tool call in the common refactoring case) or highlights the exact first divergence tick when differences exist.
