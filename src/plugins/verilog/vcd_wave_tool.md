# VCD Wave Tool (`vcd_wave`)

## Goals & Responsibilities
Generates clock-synchronous markdown waveform tables from simulation dumps (`.vcd` files).

Key features:
1. **Clock-Synchronous Sampling**: Samples signal transitions aligned with the reference clock on `posedge` (default), `negedge`, or `both` clock edges.
2. **Selective Signal Rendering**: Only evaluates and renders signals explicitly requested, preserving LLM context tokens and rendering fast.
3. **Format Prefixes**: Supports `x:<signal>` (force hexadecimal) and `b:<signal>` (force binary), defaulting to hexadecimal for multi-bit vectors and scalar 0/1 for single-bit nets.
4. **Flexible Windowing & Idle Collapsing**: Supports slicing via clock tick indices (`start_tick`, `end_tick`) or absolute simulation time units (`start_time`, `end_time`), with an enforced `max_rows` ceiling (default 50). Automatically collapses runs of $\ge 3$ consecutive unchanged cycles into concise summary rows (`collapse_idle: true`), scanning across long idle spans to maximize informative output rows.
5. **GTKWave Save File Export (`gtkw_output`)**: Exports a `.gtkw` save file pre-populated with requested signals (with hex/binary radices and slice widths), open hierarchy scopes, and a cursor/window marker anchored at the initial sample time, allowing instant visual hand-off to GUI viewers (`gtkwave dump.vcd dump.gtkw`).
6. **Temporal Provenance Row (`*(since clock)*` / `*(since time)*`)**: When windowing into a simulation (`start_tick > 0` or `start_time > 0`), prepends a compact first data row indicating the earliest clock tick (or physical timestamp) each signal continuously held its initial window value. Signals that just transitioned on the window start cycle report `new`, signals stable since tick $k$ report $k$, and signals holding their initial value since simulation start report `init`.

## Lessons Learned
- In synchronous digital designs, sampling on clock edges (`posedge`) produces clear cycle-by-cycle tabular state traces resembling cycle-accurate simulator dumps, making pipeline bugs and register state changes obvious in a single visual pass.
- Long stretches of unchanging clock cycles (e.g. wait states, reset delays, bus stalls) rapidly consume LLM context tokens without conveying new information. Collapsing runs of $\ge 3$ unchanged cycles into a single summary line while sizing `max_rows` by rendered output rows rather than raw clock ticks allows the tool to scan through idle delays and display the meaningful transitions that follow.
- Integrating GTKWave save file generation (`gtkw_output`) directly into `vcd_wave` eliminates the need for separate export round-trips while giving engineers a 1-click GUI inspection command with the exact signals, radices, and time window pinpointed by the AI assistant.
- Adding a single `*(since clock)*` provenance row on windowed queries gives instant temporal awareness without requiring the AI agent or engineer to issue speculative look-back queries. If a signal changed exactly on the window start cycle (e.g. clock 150), it reports `new` rather than repeating the redundant tick number `150`; if it has held that value since reset (clock 2) it reports `2`; and if it never changed since simulation initialization it reports `init`.
