# Verilog PNR Timing Validator (`verilog_pnr_timing_validator`)

## Goals & Constraints

- **Purpose**: Validates input arguments for `verilog_pnr_timing`, sanitizing file paths via sandbox security checks and defaulting to Reg -> Reg critical path filtering.
- **Purity**: Marked `is_pure() == true` because reading timing reports and running non-mutating temporary synthesis passes does not modify persistent user project files.

## Lessons Learned

- **Optional Path Filtering**: Supporting `path_filter: 'all'` in addition to the default `'reg_to_reg'` accommodates edge-case debugging of external board IO pads without cluttering standard clock optimization runs.
