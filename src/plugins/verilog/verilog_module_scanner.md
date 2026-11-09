# verilog_module_scanner

`verilog_module_scanner` is a helper utility for discovering Verilog/SystemVerilog source files and extracting declared module definitions within a directory or file tree.

## Goals & Constraints

- Fast, regex-assisted scanning of synthesizable `.sv` and `.v` files without invoking an external synthesis tool.
- Filters out testbenches (`tb_*`, `*_tb.sv`, `*_tb.v`, `testbench/`, `tb/`), build artifacts (`build/`, `obj_dir/`), version control and editor swap/backup files (`~`, `.bak`, `.orig`, `.org`, `#*#`).
- Accurate module declaration extraction by first stripping C-style and C++-style comments, avoiding false matches in commented-out module blocks or docstrings.
- Resolves default sidecar file paths next to the source file declaring the module (`<module_name>.timing.md`).
- Fully reusable across the MCP agent tools (`verilog_update_timing_tool`) and standalone CLI executables (`turbostar-verilog-update-timing`).

## Lessons Learned

- **Comment Stripping**: Verilog code frequently contains commented-out module headers (e.g. `// module old_alu(...)`). Regex-based module discovery directly on raw text caused dead modules or legacy headers to be synthesized, leading to false synthesis errors. Stripping comments with string-literal awareness before regex search resolved this cleanly.
- **Testbench Filtering & Discovery**: Automatically skipping testbench files and folders avoids attempting to synthesize test harnesses during synthesis, while `find_testbench_files` and `stage_readmem_files` provide the complementary testbench discovery and `$readmemh`/`$readmemb` initialization file staging needed by `verilog_simulate`.
