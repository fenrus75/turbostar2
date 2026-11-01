# VCD Compare Tool (`vcd_compare`)

## Goals & Responsibilities
Provides clock-cycle-accurate regression comparison between two VCD simulation traces (`path_a` reference vs `path_b` candidate).
Instead of inspecting voluminous raw waveform diffs, agents can immediately detect whether a regression occurred, the exact clock cycle of divergence, and which signals diverged.

Key capabilities:
1. **Clock-Synchronous Alignment**: Evaluates states synchronously on clock edges (`posedge`, `negedge`, or `both`) across cycle ticks (`0, 1, 2...`) rather than comparing raw timestamps, preventing spurious diffs due to clock period variations or timescale rounding differences.
2. **Intersection of Common Signals**: By default, compares common signals across traces while highlighting signals unique to either file as an informational note. Strict mode (`strict: true`) is available when identical manifests are required.
3. **Targeted Subsystem Scoping**: Supports restricting comparisons using `scope: "tb_main.uut"` or explicit signal lists/wildcards (`signals: ["*.A_val", "opcode"]`).
4. **Invalid Cycle Suppression (`ignore_invalid`)**: Automatically respects strobe/qualifier bindings registered via `vcd_signal_qualifier` / `vcd_label_manager`. When bus values are masked as invalid (`"-"`), differences on inactive bus cycles are ignored, preventing false positives on idle buses.
5. **Divergence Pinpointing & Wave Hint**: Identifies the earliest tick of divergence, presents a breakdown of differing signals and their values, and outputs an actionable `vcd_wave` hint centered on the divergence cycle.

## Lessons Learned
- In regression test debugging, identifying the *first* cycle of divergence is critical; subsequent cycles often cascade as divergent state spreads throughout downstream registers.
- Distinguishing between value mismatches and validity mismatches (e.g. active transaction vs unasserted strobe) gives clear diagnostic insight into whether timing or logic drifted.
