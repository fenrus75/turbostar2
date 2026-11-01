# VCD Search Tool (`vcd_search`)

## Goals & Responsibilities
Provides targeted clock-synchronous search over simulation waveforms (`.vcd` files). Instead of manually guessing tick ranges or scanning massive waveform tables, agents can search directly for specific values, binary bit patterns with wildcards, or registered label names.

Key capabilities:
1. **Clock-Synchronous Evaluation**: Samples signal values at clock edges (`posedge`, `negedge`, or `both`) using the reference clock, ensuring detected cycle intervals align 1:1 with `vcd_wave`.
2. **Flexible Value Matching**: Matches against:
   - Hexadecimal strings (`0x36`, `36`)
   - Binary patterns with wildcards (`00????10`, `b01???01`)
   - Registered signal label names (`LMA`, `FETCH`) from `vcd_label_manager`
3. **Interval Aggregation**: Automatically groups consecutive matching cycles into clean tick intervals (`"4-12, 23-59"`) with cycle counts and physical simulation time windows. Single-cycle matches are displayed as individual tick numbers (`"80"`).
4. **Direction & Selection**: Supports forward (default) and backward searches (`direction: "backward"`, `last: true`, `first: true`) with configurable limits.
5. **Actionable Follow-up Hint**: Produces a ready-to-run `vcd_wave` command centered on the first match.

## Lessons Learned
- In large simulation traces (tens of thousands to millions of cycles), querying cycle intervals directly before invoking waveform sampling saves immense context space and eliminates exploratory trial-and-error sampling loops.
- Integrating `vcd_search` directly with `vcd_label_manager` allows agents to work in terms of architecture-level domain semantics (e.g. instruction names, bus states) rather than raw machine bit patterns.
