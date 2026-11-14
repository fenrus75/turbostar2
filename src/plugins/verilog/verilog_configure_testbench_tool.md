# verilog_configure_testbench_tool

## Goals & Constraints

- **Purpose**: Manages many-to-many associations between Verilog/SystemVerilog testbench files (`tb/tb_*.sv`) and the synthesizable RTL modules they verify, along with a test duration/rigor tier (`quick`, `comprehensive`, `extreme`).
- **Persistence**: Stores testbench associations under the `"testbenches"` key and per-module default `hierarchical=true` settings under `"hierarchical_modules"` in `turbostar.json` (or `project.json`) via `fpga_project_settings`, avoiding config file clutter in the workspace.
- **Flexible Module Resolution**: Accepts either declared Verilog module names (`alu_mod`), file stems (`alu`), submodule instance names, or RTL file paths (`sv/alu/alu.sv`) and resolves them to canonical module identifiers using `verilog_module_scanner::resolve_module_name`. When `modules` is omitted, auto-discovers RTL modules directly instantiated inside the testbench.
- **Collision Detection**: Scans `$dumpfile("...")` declarations across configured and discovered testbenches and warns the agent when multiple testbenches write to the same VCD path, suggesting unique `$dumpfile` names.

## Lessons Learned

- **List Mode Purity**: Calling `verilog_configure_testbench()` with no arguments operates in read-only inspection mode, displaying configured testbenches, unconfigured workspace testbenches (and their auto-detected instantiated modules), and any `$dumpfile` VCD collisions.
- **One-Step Workspace Scan (`scan=true` / `config --scan`)**: Non-destructively scans all workspace testbenches, registers any unconfigured testbenches using their auto-discovered instantiated RTL modules, and automatically populates `hierarchical_modules` for composite parent modules (`discover_submodule_hierarchy().size() > 1`).
