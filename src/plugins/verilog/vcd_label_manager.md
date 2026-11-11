# VCD Label Manager (`vcd_label_manager`)

## Goals & Responsibilities
Provides session-persistent and file-backed value-to-label decodings for signals in VCD files (e.g. mapping opcodes, bus transaction types, and FSM state registers to human-readable names like `0x06 -> LAI`, `0x36 -> LMA`).

Key capabilities:
1. **Layered Global & Per-Waveform Persistence (`vcd_labels.json` & `<vcd_path>.labels.json`)**: Automatically loads project-wide `vcd_labels.json` first and overlays any per-waveform `<vcd_path>.labels.json` sidecar mappings on top, so labels and qualifiers registered once work across all testbenches and `.vcd` files in the project while still supporting per-file overrides.
2. **Graceful Read-Only Fallback**: If the VCD directory is read-only (e.g. read-only sandboxes or system fixtures), label updates transparently fall back to an in-memory session cache without erroring.
3. **Flexible Value & Signal Matching**: Normalizes hex, decimal, and binary input representations to standard `0x{:02x}` keys, and supports short-name or hierarchical suffix lookups.

## Lessons Learned
- In multi-turn agent debugging sessions, setting signal labels once drastically reduces token redundancy in subsequent `vcd_wave` invocations while keeping waveform tables readable.
- Centralizing signal pattern matching via `vcd_label_manager::signal_pattern_matches()` allows both `find_label()` and validation tools (`vcd_signal_labels`) to share identical matching semantics for leaf names, hierarchy wildcards (`uut.*.A_val`), and suffix matching.
- Agents frequently re-register broader or narrower glob patterns (e.g. `*opcode*` after `*opcode`) when refining decodings mid-session. Removing conflicting hex value keys from overlapping patterns on registration and ranking matching patterns by literal specificity prevents stale earlier bindings from shadowing newer labels.
- Tracking one-shot hint consumption flags (`try_consume_qualifier_hint`, `try_consume_label_hint`) inside `vcd_label_manager` keeps session-level hint deduplication aligned with `clear_all()` resets in tests and plugin reloads.
- Storing mappings in a project-global `vcd_labels.json` by default (`global: true`) eliminates duplicate setup across multiple testbenches and `.vcd` files (and avoids `.gitignore` patterns that ignore `*.vcd*`).
