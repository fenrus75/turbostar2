# VCD File Info Tool (`vcd_file_info`)

## Goals & Responsibilities
Provides digital engineers and AI agents with simulation metadata and signal discovery for IEEE 1364/1800 Value Change Dump (`.vcd`) files.

Key features:
1. **Summary Header**: Reports timescale, simulation start/end times, generator, and detected/specified reference clock with period and tick counts.
2. **Signal Filtering**: Enables agents to filter thousands of netlist signals down to relevant sub-modules via `scope` (e.g. `tb_main.uut.cpu8008`) or wildcards via `filter` (e.g. `*addr*`, `*write*`).
3. **Context Safety**: Protects the LLM conversation context from flooding by applying `max_signals` truncations with clear continuation counters.
4. **Static Signal Annotation & Toggle Stats (`stats`)**: Automatically annotates signals that never toggle throughout the simulation with `**` (e.g. `` `tb_main.uut.error_flag`** ``) and provides a bottom summary note. When `stats: true` is passed, expands the table with cycle of first toggle (`First Toggle (Tick)`) and cycle transition percentage (`Toggle Rate`). Supports windowing via `start_tick` and `end_tick`.

## Lessons Learned
- Verilator-generated VCDs contain hierarchical alias signals mapping to identical internal VCD wire IDs. Listing the full canonical hierarchy allows agents to copy-paste exact signal paths directly into subsequent `vcd_wave` requests.
- Many verification failures (such as stuck-at-reset or un-asserted error flags) manifest as signals that never transition. Annotating static signals with `**` in the default view highlights anomalies at zero extra token cost, while `stats: true` provides full cycle-accurate toggle percentages on demand.
- Placing the unit name `(Tick)` in the table column header rather than repeating `"tick "` on every row saves significant tokens when reporting dozens of signals.
- Under synchronous posedge sampling, the reference clock signal itself samples identical values (`1`) on every cycle. Reporting `[ref clk]` instead of `0%` prevents confusing LLM agents and human reviewers into thinking the clock failed to toggle.
- Whole-trace toggle stats can dilute signal behavior for long simulations where a signal is active only during a specific burst. Adding optional `start_tick` and `end_tick` parameters allows targeting specific time ranges (e.g. around an instruction store or peripheral handshake).
