# VCD Parser (`vcd_parser`)

## Goals & Responsibilities
`vcd_parser` provides an in-memory streaming and indexing engine for IEEE 1364/1800 Value Change Dump (`.vcd`) simulation files.

Key capabilities:
1. **Header Parsing**: Fast parsing of timescale, date, version, hierarchical scopes, and signal declarations with aliases and bus slices.
2. **Clock Analysis**: Automatic detection and transition counting of periodic reference clocks, period estimation, and posedge/negedge counting.
3. **Clock-Synchronous Waveform Sampling**: Samples signal state vectors exactly at clock transitions (`posedge`, `negedge`, or `both`) across specified clock tick ranges or simulation time windows.
4. **Signal Formatting**: Automatically formats multi-bit vectors into readable hexadecimal (`0x36`) by default, while supporting explicit format overrides (`b:` for binary, `x:` for hex).

## Lessons Learned
- VCD variable identifiers are frequently shared across multiple hierarchical scopes (e.g. connected ports and module wires). The parser maintains an ID-to-signal multimap to avoid duplicate state tracking while allowing any hierarchical alias to be queried.
- Simulators like Verilator omit `$dumpvars` tags in certain tracing configurations. Parsing body value changes directly after `$enddefinitions $end` ensures compatibility across all standard simulator outputs.
