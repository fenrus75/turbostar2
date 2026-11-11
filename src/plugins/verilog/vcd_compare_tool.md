# VCD Compare Tool (`vcd_compare`)

## Goals & Responsibilities
Provides clock-cycle-accurate regression comparison between two VCD simulation traces (`path_a` reference vs `path_b` candidate).
Instead of inspecting voluminous raw waveform diffs, agents can immediately detect whether a regression occurred, the exact clock cycle of divergence, and which signals diverged.

Key capabilities:
1. **Clock-Synchronous Alignment**: Evaluates states synchronously on clock edges (`posedge`, `negedge`, or `both`) across cycle ticks (`0, 1, 2...`) rather than comparing raw timestamps, preventing spurious diffs due to clock period variations or timescale rounding differences.
2. **Intersection of Common Signals**: By default, compares common signals across traces while highlighting signals unique to either file as an informational note. Strict mode (`strict: true`) is available when identical manifests are required.
3. **Targeted Subsystem Scoping**: Supports restricting comparisons using `scope: "tb_main.uut"` or explicit signal lists/wildcards (`signals: ["*.A_val", "opcode"]`).
4. **Symmetric Qualifier Unification**: Qualifiers from either trace sidecar are unified and applied symmetrically to both traces during comparison. This prevents asymmetric masking foot-guns where a strobe registered on one file causes false validity mismatches on cycle 0.
5. **Invalid Cycle Suppression (`ignore_invalid`)**: When bus values are masked as invalid (`"-"`) on inactive strobe cycles, differences are ignored. If one trace asserts a valid strobe while the other does not, it is reported as a distinct `Validity mismatch` (`- (invalid)` vs concrete value).
6. **In-Sync Prefix & Divergence Metrics**: Reports the exact number of initial cycles running in lockstep before divergence (`In-Sync Prefix`), the earliest cycle of divergence with all differing signals, and the subsequent drift cascade percentage.

## Lessons Learned
- In regression test debugging, identifying the *first* cycle of divergence is critical; subsequent cycles often cascade as divergent state spreads throughout downstream registers.
- Distinguishing between value mismatches and validity mismatches (e.g. active transaction vs unasserted strobe) gives clear diagnostic insight into whether timing or logic drifted.
- Sidecar qualifiers must always be applied symmetrically across both files under comparison; otherwise, an unmasked golden file compared against a masked candidate file triggers false positive validity mismatches at tick 0 before simulation even begins.
- Providing `vcd_compare_tool::quick_compare` allows `verilog_simulate` to perform an immediate in-memory comparison against `<stem>.previous.vcd` and `<stem>.lastpass.vcd` using `vcd_parser_cache` without requiring a separate agent tool round-trip when traces match.
