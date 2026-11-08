# VCD Label Manager (`vcd_label_manager`)

## Goals & Responsibilities
Provides session-persistent and file-backed value-to-label decodings for signals in VCD files (e.g. mapping opcodes, bus transaction types, and FSM state registers to human-readable names like `0x06 -> LAI`, `0x36 -> LMA`).

Key capabilities:
1. **Sidecar Persistence (`<vcd_path>.labels.json`)**: Automatically loads and updates companion JSON files alongside simulation traces so that custom annotations persist across editor restarts and agent sessions.
2. **Graceful Read-Only Fallback**: If the VCD directory is read-only (e.g. read-only sandboxes or system fixtures), label updates transparently fall back to an in-memory session cache without erroring.
3. **Flexible Value & Signal Matching**: Normalizes hex, decimal, and binary input representations to standard `0x{:02x}` keys, and supports short-name or hierarchical suffix lookups.

## Lessons Learned
- In multi-turn agent debugging sessions, setting signal labels once drastically reduces token redundancy in subsequent `vcd_wave` invocations while keeping waveform tables readable.
- Centralizing signal pattern matching via `vcd_label_manager::signal_pattern_matches()` allows both `find_label()` and validation tools (`vcd_signal_labels`) to share identical matching semantics for leaf names, hierarchy wildcards (`uut.*.A_val`), and suffix matching.
- Agents frequently re-register broader or narrower glob patterns (e.g. `*opcode*` after `*opcode`) when refining decodings mid-session. Removing conflicting hex value keys from overlapping patterns on registration and ranking matching patterns by literal specificity prevents stale earlier bindings from shadowing newer labels.
