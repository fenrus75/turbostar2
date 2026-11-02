# VCD Parser (`vcd_parser`)

## Goals & Responsibilities
`vcd_parser` provides an in-memory streaming and indexing engine for IEEE 1364/1800 Value Change Dump (`.vcd`) simulation files.

Key capabilities:
1. **Header Parsing**: Fast parsing of timescale, date, version, hierarchical scopes, and signal declarations with aliases and bus slices.
2. **Clock Analysis**: Automatic detection and transition counting of periodic reference clocks, period estimation, and posedge/negedge counting.
3. **Clock-Synchronous Waveform Sampling**: Samples signal state vectors exactly at clock transitions (`posedge`, `negedge`, or `both`) across specified clock tick ranges or simulation time windows.
4. **Signal Formatting**: Automatically formats multi-bit vectors into readable hexadecimal (`0x36`) by default, while supporting explicit format overrides (`b:` for binary, `x:` for hex).
5. **Cycle-Accurate Toggle Statistics**: Evaluates signal toggle counts, first toggle clock ticks, and cycle-by-cycle activity rates synchronously against reference clocks or asynchronously, with optional tick interval windowing (`start_tick`/`end_tick`).
6. **Temporal Value Provenance**: Computes the earliest physical timestamp (`get_signal_since_time`) or reference clock tick (`get_signal_since_tick`) a signal has held its current value continuously without modification, returning `std::nullopt` for values established prior to the first clock edge (initialization).

## Lessons Learned
- VCD variable identifiers are frequently shared across multiple hierarchical scopes (e.g. connected ports and module wires). The parser maintains an ID-to-signal multimap to avoid duplicate state tracking while allowing any hierarchical alias to be queried.
- Simulators like Verilator omit `$dumpvars` tags in certain tracing configurations. Parsing body value changes directly after `$enddefinitions $end` ensures compatibility across all standard simulator outputs.
- In complex designs, agents often request signals by short name or partial hierarchy (e.g. `block2.reset` after `block1.block2.clock`). Suffix and leaf matching with contextual scope preference allows seamless disambiguation of shorthand signal queries.
- When sampling waveform traces with idle collapsing enabled, tracking pending repeats with constant memory ($O(1)$ sample points) and flushing either individual rows ($< 3$ repeats) or single collapsed summary ranges ($\ge 3$ repeats) prevents memory bloat while preserving cycle count math.
- When inspecting windowed waveform regions starting at $t > 0$ or tick $> 0$, signals displayed on the first row may have transitioned on that exact cycle or held that value since early in simulation. Computing backward transition stability and mapping back to clock tick indices via `lower_bound` on clock edges provides instant temporal provenance without scanning preceding cycles. Distinguishing values established before the first clock edge (`nullopt`, rendered as `init`) from values clocked on tick 0 (`0`) or actively transitioning on the window start cycle (`now`) completely resolves semantic ambiguity for agents and human engineers alike.
