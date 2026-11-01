# VCD Wave Tool (`vcd_wave`)

## Goals & Responsibilities
Generates clock-synchronous markdown waveform tables from simulation dumps (`.vcd` files).

Key features:
1. **Clock-Synchronous Sampling**: Samples signal transitions aligned with the reference clock on `posedge` (default), `negedge`, or `both` clock edges.
2. **Selective Signal Rendering**: Only evaluates and renders signals explicitly requested, preserving LLM context tokens and rendering fast.
3. **Format Prefixes**: Supports `x:<signal>` (force hexadecimal) and `b:<signal>` (force binary), defaulting to hexadecimal for multi-bit vectors and scalar 0/1 for single-bit nets.
4. **Flexible Windowing**: Supports slicing via clock tick indices (`start_tick`, `end_tick`) or absolute simulation time units (`start_time`, `end_time`), with an enforced `max_ticks` ceiling.

## Lessons Learned
- In synchronous digital designs, sampling on clock edges (`posedge`) produces clear cycle-by-cycle tabular state traces resembling cycle-accurate simulator dumps, making pipeline bugs and register state changes obvious in a single visual pass.
