# VCD Signal Qualifier Tool (`vcd_signal_qualifier`)

## Goals & Responsibilities
Enables agents to declare validity relationships between signals (e.g. data bus `opcode_data` qualified by control line `opcode_data_valid`, or memory bus qualified by active-low chip select `~cs_n`).

Key capabilities:
1. **Automatic `-` Masking**: When a qualifier signal is deasserted (`0` for active-high, `1` for active-low, or `x`/`z`), the qualified signal value is replaced with `"-"`.
2. **Idle Cycle Collapsing**: Treating invalid signal states as constant `"-"` prevents residual bus wiggles or floating bits from breaking idle cycle collapsing in `vcd_wave`.
3. **Accurate Search (`vcd_search`)**: Ensures `vcd_search` ignores invalid cycles, preventing spurious matches on un-driven or inactive buses.
4. **Project-Global & Sidecar Persistence**: Saves qualifier bindings into project-wide `vcd_labels.json` by default (`global: true`) or into `<vcd_path>.labels.json` when `global: false`, with graceful in-memory fallback in read-only directories.
5. **Wildcard and Relative Scope Resolution**: Supports leaf names (`"data" -> "valid"`), wildcards (`"uut.*.data" -> "uut.*.valid"`), and prefix matching.

## Lessons Learned
- In synchronous digital interfaces, signals frequently hold leftover values from earlier transactions when the handshake line is low. Masking un-qualified cycles as `"-"` eliminates false positive searches and keeps waveform tables clean and concise.
- Treating `x` and `z` qualifier states as invalid avoids spurious matches during reset phases or uninitialized simulation startup intervals.
