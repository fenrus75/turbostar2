# verilog_update_timing_validator

## Goals & Constraints
- Validates parameter inputs for the `verilog_update_timing` tool.
- Validates read permissions on the target directory/file and optional netlist path.
- Configured as impure (`is_pure() == false`) since it writes `<module>.timing.md` sidecar files to disk.

## Lessons Learned
- Batch sidecar updates require write access to each module's parent directory; setting `is_pure: false` correctly restricts this tool to read-write agent sessions.
