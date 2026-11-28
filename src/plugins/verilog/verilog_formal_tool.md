# `tools::verilog_formal_tool`

Runs SymbiYosys (`sby`) formal verification (`bmc`, `prove`, `cover`, or `all`) on SystemVerilog/Verilog modules with `bind`-based property files in `formal/`.

## Goals & Constraints
- **Transitive Submodule & Data Staging**: Resolves all transitive `.sv` dependencies via `verilog_module_scanner::discover_submodule_hierarchy` and stages `$readmem` init files into the SBY work directory.
- **Slang Frontend for `bind`**: Uses `plugin -i slang` + `read_slang --top <mod> -DFORMAL` so SystemVerilog `bind` statements work with open-source Yosys (falling back to `read -sv -formal` when `slang` is unavailable).
- **Solver & Tool Discovery**: Resolves `sby` from `fpga.sby_path` (`turbostar.json`) or `$PATH` (with `~/` expansion), prepends `sby`'s directory to `PATH` for `oss-cad-suite` binaries, and auto-detects `bitwuzla` -> `yices` -> `z3`.
- **Actionable Diagnostics**: Parses per-coverpoint `HIT (step N)` / `UNREACHABLE` results with vacuity warnings when `bmc`/`prove` passes while coverpoints are unreachable (plus a non-fatal note when `assume(...)` constraints are present with zero `cover(...)` checks); audits declared `output`/`inout` ports against `assert` expressions (including local helper assignments and `bind` remappings) and emits a non-fatal note for unchecked output ports; on assertion failure, displays the `-->` ±2-line source snippet and copies the counterexample VCD to `wave/formal_<mod>_<task>.vcd` for `vcd_wave`.

## Lessons Learned
- Open-source `yosys` `read_verilog -sv` rejects SystemVerilog `bind`; `oss-cad-suite`'s `plugin -i slang` + `read_slang` handles `bind` cleanly and supports `assert`/`assume`/`cover`.
- Expanding leading `~/` in `$PATH` entries is essential when users add `~/external/oss-cad-suite/bin` to `$PATH`.
