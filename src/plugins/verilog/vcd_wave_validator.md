# VCD Wave Validator (`vcd_wave_validator`)

## Goals & Responsibilities
Validates parameters and constructs instances of `vcd_wave_tool`.

Key responsibilities:
1. Validates required inputs (`path`, `signals`).
2. Performs filesystem security sandbox checks on `path` (read access) and `gtkw_output` (write access).
3. Validates and parses optional parameters including `clock_signal`, `edge`, `start_tick`, `end_tick`, `start_time`, `end_time`, `max_rows` (and deprecated alias `max_ticks`), `collapse_idle`, and `gtkw_output`.

## Lessons Learned
- Using strict filesystem sandbox validation (`ctx.fs_security.validate_access`) with appropriate permissions (`read` for input VCD, `write` for GTKWave save file) ensures path traversal attempts are caught before tool execution.
