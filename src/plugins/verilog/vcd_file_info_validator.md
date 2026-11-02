# VCD File Info Validator (`vcd_file_info_validator`)

## Goals & Responsibilities
Validates arguments for `vcd_file_info_tool` and builds tool instances.

Key responsibilities:
1. Validates required argument `path` and performs filesystem read permission validation.
2. Validates optional arguments: `clock_signal`, `scope`, `filter`, `max_signals`, `stats`, `start_tick`, and `end_tick`.
3. Automatically enables `stats: true` when `start_tick` or `end_tick` is provided, and checks that `start_tick <= end_tick`.

## Lessons Learned
- Providing clear default values and type validation on optional parameters ensures tools behave predictably under missing or partial input JSON payloads.
- Automatically enabling `stats` when window bounds (`start_tick`/`end_tick`) are passed improves agent usability and prevents silently ignoring window parameters.
