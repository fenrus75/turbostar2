# verilog_module_scanner

`verilog_module_scanner` is a helper utility for discovering Verilog/SystemVerilog source files and extracting declared module definitions within a directory or file tree.

## Goals & Constraints

- Fast, regex-assisted scanning of synthesizable `.sv` and `.v` files without invoking an external synthesis tool.
- Filters out testbenches (`tb_*`, `*_tb.sv`, `*_tb.v`, `testbench/`, `tb/`), build artifacts (`build/`, `obj_dir/`), version control and editor swap/backup files (`~`, `.bak`, `.orig`, `.org`, `#*#`).
- Accurate module declaration extraction by first stripping C-style and C++-style comments, avoiding false matches in commented-out module blocks or docstrings.
- Fully reusable across the MCP agent tools (`verilog_update_timing_tool`) and standalone CLI executables (`turbostar-verilog-timing`).
## Lessons Learned

- **Comment Stripping**: Verilog code frequently contains commented-out module headers (e.g. `// module old_alu(...)`). Regex-based module discovery directly on raw text caused dead modules or legacy headers to be synthesized, leading to false synthesis errors. Stripping comments with string-literal awareness before regex search resolved this cleanly.
- **Testbench Filtering & Discovery**: Automatically skipping testbench files and folders avoids attempting to synthesize test harnesses during synthesis, while `find_testbench_files` and `stage_readmem_files` provide the complementary testbench discovery and `$readmemh`/`$readmemb` initialization file staging needed by `verilog_simulate`.
- **Module Name Resolution Across Files & Instances**: RTL filenames and submodule instance names often differ from the declared Verilog `module` identifier (e.g. `alu.sv` declaring `alu_mod`, or `cpu cpu8008` in `main.sv`). `resolve_module_name` uses `discover_modules_in_files` and instantiation scanning to resolve file stems, `_mod` suffixes, and instance names back to the declared RTL module identifier.
- **Fast SystemVerilog Enum Auto-Discovery (`mapped_file` + `::memmem`)**: `discover_enum_signal_labels` reuses the `mapped_file` cache and `::memmem` whole-file substring pre-filtering (inspired by `fs_grep_files`) to skip files that do not contain `"enum"`, discovered enum `type_name`s, or target `module_name`s before stripping comments or running regexes. It extracts `typedef enum` and inline `enum` constants, maps signals declared with those enum types inside modules and their instantiated hierarchy names (e.g. `*sequencer.state`, `*sequencer.next_state`), and provides zero-config enum labels to `vcd_label_manager`.
- **Submodule Hierarchy & Testbench Instantiation Discovery**: `discover_instantiated_modules` and `discover_submodule_hierarchy` scan module bodies (after the port header `;`) to identify instantiated RTL modules and compute a deduplicated bottom-up (post-order) module dependency list for hierarchical testbench execution.
