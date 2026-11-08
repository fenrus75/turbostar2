# Verilog Project Settings Validator (`verilog_project_settings_validator`)

## Goals & Constraints

- **Purpose**: Validates input arguments for `verilog_project_settings`, sanitizing parameters and ensuring type safety.
- **Purity**: Marked `is_pure() == false` because updating project settings modifies `turbostar.json` or `project.json` on disk.

## Lessons Learned

- **Optional Args Schema**: Making all parameters optional allows a bare invocation `verilog_project_settings()` to serve as an environmental inspection and guidance query.
