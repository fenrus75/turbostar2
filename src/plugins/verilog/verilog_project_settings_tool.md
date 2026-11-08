# Verilog Project Settings Tool (`verilog_project_settings_tool`)

## Goals & Constraints

- **Purpose**: Implements the `verilog_project_settings` tool action, allowing agents to query or configure FPGA target parameters (`target`, `device`, `package`, `top_module`, `freq_mhz`, `pcf_file`, `sv_path`, `yosys_synth_args`) persisted in `turbostar.json` or `project.json`.
- **Informative Reporting**: When queried or partially configured, reports discovered EDA tool binaries, detected pin constraints, current configuration, and actionable guidance for missing parameters.

## Lessons Learned

- **Automatic Single PCF Selection**: When saving settings and no PCF is specified, if exactly one `.pcf` exists in the repository, auto-associating it saves the user from redundant manual entry.
- **Active Config File Tracking Post-Save**: When saving FPGA target configuration, updating `cfg.active_config_file` with the written path (`path_used`) before rendering `format_summary` prevents the summary header from claiming `Config File: None (not configured)` immediately after a successful write.
