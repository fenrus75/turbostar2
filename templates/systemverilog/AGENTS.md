# @@PROJECT_NAME@@ Development Guidelines

Important: Activate the "verilog" tool family when working on this project to enable the Verilog/FPGA-specific tools.

## Project Specific Targets (Edit/Update this section as information becomes available)

- Target Device: <device> (FPGA or ASIC)
- Target Frequency: <target> MHz
- Target Reg-Reg max logic levels: <levels>


## Fundamental FPGA external tools
This project uses `yosys` and `verilator` for synthesis and simulation, and `nextpnr` for place-and-route.
Limit the use of SystemVerilog language constructs to those supported by both `yosys` and `verilator`.

## Directory structure
- `src/` for main RTL SystemVerilog files (`*.sv`)
- `tb/` for testbenches
- `wave/` for generated waveform outputs 
    - give each testbench a unique filename such as `$dumpfile("wave/dump_<tb>.vcd")` so parallel testbench runs do not collide, and add `wave/` to `.gitignore`. (*If your project prefers a different location for waveforms, change this one convention and adjust the `$dumpfile` strings accordingly.*)

## SystemVerilog guidelines
- Default: one module per `.sv` file (possible exception: very small local helper modules)
- Option: group related modules into subdirectories within `src/` for logical organization
- Use modern SystemVerilog best practices (within the subset supported by both `yosys` and `verilator`):
  - `always_ff` for flip-flops/registers
  - `always_comb` for all combinational logic (assign default values to all outputs at the top of `always_comb` blocks to avoid accidental inferred latches, which FPGA targets such as `synth_ice40` reject as errors)
  - favor `logic` over `reg` or `wire` for signals and wires; replace legacy `wire`/`reg` occurrences with `logic` when editing components
  - use `typedef enum` for FSM states
  - use `unique case` for state machines as much as possible; mandatory for the FSM case
  - always define a `default:` case in case statements.
- For larger designs where the top levels are exclusively modules connected by wires, avoid adding direct logic
	- instead create new modules or integrate the logic into existing modules.
- Use the `_q` suffix when one signal is a one-cycle delayed version of another signal, `_q2` for two cycles and so on.
- Use the `_n` suffix convention for active-low signals
- Reset polarity: Pick one reset polarity (rst or rst_n) for the whole design


## Guidelines for high frequency designs (non-normative)
- **Goal & budget:** Set a frequency target from the start (ask if missing) and document the `Reg -> Reg` logic-level budget in `Project Specific Targets`.
- **Design for speed upfront:** Avoid leaving non-critical paths sloppy—synthesis tools optimize better with fewer near-critical paths, and retrofitting timing in large designs is costly.
- **Resets:** Avoid resetting internal registers unless required for correctness, **except** keep one uniform reset strategy per module (see *Control-Set Homogeneity* below). Never mix sync and async resets, and **strongly** avoid non-zero reset values.
- **Structure:** Prefer `case` statements over long `if-else` chains. Register module outputs whenever possible (exception: registering a RAM output directly, even on a downstream module's input, overrides this).
- **High fanout:** Duplicate output registers/signals (`_dup1`, `_dup2`, ...) when fanout limits timing.
- **Load-enable register** (prefer synchronous reset on FPGA targets):
```verilog
    always_ff @(posedge clk) begin
        if (rst)       value <= 8'h00;
        else if (load) value <= load_value;
    end
```

## Guidelines for power optimization (non-normative)
- Secondary to meeting timing constraints; avoid async resets where possible.
- **Valid-bit gating:** When a registered output has a valid bit and timing allows, clock `'0` when invalid to reduce dynamic power ($C_{\mathrm{dyn}}$):
```verilog
always_ff @(posedge clk) begin
	valid_out <= data_valid;
	// Maps directly to the FF synchronous reset pin on most FPGAs
	data_out  <= data_valid ? data : '0;
end
```


## Testing strategy
- Write at least one testbench per module (split basic vs. comprehensive, or split large test spaces into multiple parallel testbenches with unique `$dumpfile` names).
- Emit explicit `PASS` on success and `FAIL` / `$error` / `$fatal` on failure so `verilog_simulate()` can verify results (benches without assertions report `INFO`).
- Use `verilog_configure_testbench()` to bind testbenches in `turbostar.json` (set `hierarchical=true` on integration/top modules).
- For bugs, write a failing reproducing test first, then fix the RTL and verify it passes.
- Run tests with `verilog_simulate()`; passing runs snapshot `.lastpass.vcd` and every run diffs against `.previous.vcd` to isolate the first diverging cycle/signal (use `vcd_compare()` to diff any two VCDs directly).

## Edit process
- Use the dedicated tools for the standard "edit - compile - test - commit" cycle:
  - `fs_replace_lines` / `fs_write_file` for editing files
  - `verilog_compile` for syntax checking and synthesis health (use `lint_only=true` for a fast first pass; full log in `tmp://synthesis.log`)
  - `verilog_simulate` for running the test cases
  - `verilog_pnr_timing` for validating timing optimizations (if appropriate)
  - `verilog_update_timing` to update timing annotations in the codebase
  - `git_add` / `git_commit` to create the git commit

## Tool usage
- Initial setup: Call `verilog_project_settings()` to set target FPGA, device, package, top module, target frequency, `sv_path="src"`, and optional `yosys_synth_args` / `nextpnr_args` (e.g., `--tmg-ripup`, `--opt-timing`).
- Waveforms: Use `vcd_signal_qualifier()` to mask invalid bus cycles, `vcd_signal_labels()` to map opcodes/values to names, `vcd_wave(instance=...)` / `vcd_search()` / `vcd_compare()` to inspect traces, and `vcd_signal_to_code()` to trace nets to RTL drivers/sinks.

## Timing optimization strategy
- Combine structural and physical views:
  - `verilog_logic_depth`: LUT depth between FFs (`Reg -> Reg`, `In -> Reg`, `Reg -> Out`).
  - `verilog_update_timing`: updates in-place `// [timing: ...]` port comments in `.sv` files.
  - `verilog_pnr_timing`: post-route $F_{\max}$, logic vs. wire delay, and fanout.
- When an RTL refactor shrinks LUT area without moving `verilog_pnr_timing` $F_{\max}$, check `In -> Reg` and `Reg -> Out` paths in `verilog_logic_depth` to see if a decode cone or module boundary took over.
- Judge PnR changes by the **multi-seed median** $F_{\max}$ rather than single-seed luck.
- If an optimization fails, revert it and record the dead end in a git-tracked note so it is not repeated.


## Learnings

### "Control-Set Homogeneity Beats Reset Stripping" (FPGA)
  Within a module, it is better for all flip-flops to share the same reset style (e.g. all `always_ff @(posedge clk)` with synchronous reset) than to
  selectively strip reset from half of them. Keeping control sets identical allows `nextpnr` to pack flip-flops tightly into single physical
  tiles with zero inter-tile routing hops. This rule overrides other guidance that recommends not using `rst` - many real-design simulations
  have shown that it is more important to have homogeneity than to do the avoid-rst optimization (which is valid in the more general case).
