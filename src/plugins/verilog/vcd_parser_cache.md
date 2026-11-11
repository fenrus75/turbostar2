# VCD Parser Cache (`vcd_parser_cache`)

## Goals & Responsibilities
`vcd_parser_cache` provides a thread-safe, in-memory LRU cache of parsed `vcd_parser` instances across tool invocations in the Verilog plugin family.

Key capabilities:
1. **Zero-Latency Repeated Lookups**: Consecutive tool calls (`vcd_file_info`, `vcd_search`, `vcd_wave`, `vcd_signal_to_code`) against the same simulation trace reuse the previously parsed AST and transition map, turning multi-second re-parses into sub-millisecond cache hits.
2. **2-Slot LRU Capacity**: Sized to hold 2 trace files concurrently. This specifically prevents cache thrashing during differential trace analysis via `vcd_compare`, where two distinct simulation files (e.g., golden vs. regression run) are evaluated side-by-side.
3. **Stat-Based Staleness Invalidation**: Validates cached files using filesystem modification timestamp (`last_write_time`) and `file_size` on every request. If a trace is re-simulated or touched by an external build tool, the cache automatically discards the stale entry and re-parses.
4. **Lock-Free File I/O**: Heavy file reading and token parsing are executed outside the mutex lock; the mutex is acquired exclusively for short lookup, promotion, and vector insertion steps.

## Lessons Learned
- AI agent debugging workflows naturally involve multi-turn sequences: querying metadata, searching for triggers, and multiple waveform samplings. Without in-memory caching, each turn in the conversation re-parses the identical multi-megabyte VCD file from disk from scratch.
- Sizing the LRU cache at 2 entries perfectly serves both single-file deep dive workflows and dual-file differential verification (`vcd_compare`) without noticeable memory overhead.
- During iterative RTL optimization, agents sometimes inspect an existing `.vcd` waveform after editing `.sv`/`.v` sources without re-running simulation first. Checking RTL source modification timestamps against the VCD file's mtime (`check_stale_rtl_warning`) across sibling `sv/`, `rtl/`, and `src/` directories (even when VCDs are placed under `wave/`, `waves/`, or `sim/`) surfaces an immediate warning banner in `vcd_file_info`, `vcd_wave`, and `vcd_search`.
