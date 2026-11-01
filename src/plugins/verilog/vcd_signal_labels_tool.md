# VCD Signal Labels Tool (`vcd_signal_labels`)

## Goals & Responsibilities
Enables agents to assign human-readable names (e.g. instruction mnemonics, transaction names, FSM states) to signal values in a VCD trace.

Key features:
1. **Sidecar Auto-Save**: Attempts to save mappings into `<vcd_path>.labels.json` so they persist across sessions.
2. **In-Memory Fallback**: Retains mappings in memory even if the file is in a read-only directory or sandbox.
3. **Automatic `vcd_wave` Integration**: Once labels are defined, `vcd_wave` automatically decorates matching signals with their label in parentheses: `0x06 (LAI)`.
4. **Clearing**: Supports clearing labels for a specific signal or all signals via `clear: true`.

## Lessons Learned
- Providing an instant summary of all active labels in the tool output lets the agent verify that hex values and labels were normalized and mapped as expected.
