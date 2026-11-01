# VCD File Info Tool (`vcd_file_info`)

## Goals & Responsibilities
Provides digital engineers and AI agents with simulation metadata and signal discovery for IEEE 1364/1800 Value Change Dump (`.vcd`) files.

Key features:
1. **Summary Header**: Reports timescale, simulation start/end times, generator, and detected/specified reference clock with period and tick counts.
2. **Signal Filtering**: Enables agents to filter thousands of netlist signals down to relevant sub-modules via `scope` (e.g. `tb_main.uut.cpu8008`) or wildcards via `filter` (e.g. `*addr*`, `*write*`).
3. **Context Safety**: Protects the LLM conversation context from flooding by applying `max_signals` truncations with clear continuation counters.

## Lessons Learned
- Verilator-generated VCDs contain hierarchical alias signals mapping to identical internal VCD wire IDs. Listing the full canonical hierarchy allows agents to copy-paste exact signal paths directly into subsequent `vcd_wave` requests.
