# VCD File Info Validator (`vcd_file_info_validator`)

## Goals & Responsibilities
Validates arguments for `vcd_file_info_tool` and builds tool instances.

Key responsibilities:
1. Validates required argument `path` and performs filesystem read permission validation.
2. Validates optional arguments: `clock_signal`, `scope`, `filter`, `max_signals`, and `stats`.

## Lessons Learned
- Providing clear default values and type validation on optional parameters ensures tools behave predictably under missing or partial input JSON payloads.
