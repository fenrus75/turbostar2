# Verilog Project Settings Tool (`verilog_project_settings_tool`)

## Goals & Constraints

- **Purpose**: Implements the `verilog_project_settings` tool action, allowing agents to query or configure FPGA target parameters (`target`, `device`, `package`, `top_module`, `freq_mhz`, `pcf_file`, `yosys_synth_args`) persisted in `turbostar.json` or `project.json`.
- **Informative Reporting**: When queried or partially configured, reports discovered EDA tool binaries, detected pin constraints, current configuration, and actionable guidance for missing parameters.

## Lessons Learned

- **Automatic Single PCF Selection**: When saving settings and no PCF is specified, if exactly one `.pcf` exists in the repository, auto-associating it saves the user from redundant manual entry.
