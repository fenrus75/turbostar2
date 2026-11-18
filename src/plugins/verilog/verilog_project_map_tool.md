# `verilog_project_map_tool`

Pure read-only onboarding tool that maps a SystemVerilog/Verilog project's module instantiation hierarchy, file/subdirectory layout, and testbench coverage in a single call.

## Goals & Constraints
- **Zero External Tools**: Uses `verilog_module_scanner` + `mapped_file::get_cached` + `fpga_project_settings` (no Yosys or Verilator invocations) so it completes in milliseconds.
- **Compact Output**: Collapses repeated identical sibling instantiations (`mod xN`) and caps per-directory file lists at 12 filenames so large projects stay concise.

## Lessons Learned
- Initial implementation: groups RTL and testbench files by relative parent directory so agents immediately know which paths to pass to `fs_read_lines()`.
