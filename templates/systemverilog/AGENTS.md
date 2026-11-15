# @@PROJECT_NAME@@ Development Guidelines

Important: Activate the "verilog" tool family when working on this project to enable the Verilog/FPGA-specific tools.

## Project Specific Targets (Edit/Update this section as information becomes available)

- Target Device: <device> (FPGA or ASIC)
- Target Frequency: <target> Mhz
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
  - `always_comb` for all combinational logic
  - favor `logic` over `reg` or `wire` for signals and wires, replace legacy wire/reg occurances with logic when editing components
  - use `typedef enum` for FSM states
  - use `unique case` for statemachines as much as possible; mandatory for the FSM case
  - always define a `default:` case in case statements.
- For larger designs where the top levels are exclusively modules connected by wires, avoid adding direct logic
	- instead create new modules or integrate the logic into existing modules.
- Use the `_q` suffix when one signal is a one-cycle delayed version of another signal, `_q2` for two cycles and so on.
- Use the `_n` suffix convention for active-low signals
- Reset polarity: Pick one reset polarity (rst or rst_n) for the whole design


## Guidelines for high frequency designs (non-normative)
- Know the Goal: Set a frequency goal from the start - when not already provided, ask the user.
    - Logic Levels: Use target device knowledge to make and document an estimate for the number of reg->reg logic levels that can support this frequency
    - Use the `verilog_update_timing` toolcall after major changes to keep in-code logic level annotations updated
- Performance from the start: Always write performing implementations from the very beginning even for non-critical-path components
	- Synthesis tools work better if there are only few near-critical-timing paths
	- Retrofitting performance in large designs is much harder than to just create good design from the start
- Reset Reduction: Avoid using reset signals to internal registers for which reset behavior is not required for correctness
	- for FPGA designs, use only one reset strategy within a module (see "Control-Set Homogeneity Beats Reset Stripping") or within closely related modules
- Async-reset: Don't mix async and sync reset in the design
- Non-zero reset values: **STRONGLY** avoid non-0 reset values whenever the design allows
- Case statements instead of long if-then-else chains
- Register the output: For modules that contain registers, favor registering the final output whenver possible over registering intermediate results.
	- Exception: If it is possible to register the output of a RAM directly, always do this even if it happens at the input side of another module. This rule overrides the "register the output" general guidance.
- High-fanout: Duplicate output registers and signals if high-fanout after the module is a fundamental timing issue. Suffix duplicated outputs with `_dup1`, `_dup2` etc suffixes
- Load-enable register: For FPGA targets, use this construct for registers with a load/enable input (example shown with synchronous reset, adjust as needed):
```verilog
    always_ff @(posedge clk, posedge rst) begin
        if (rst)       value <= 8'h00;
        else if (load) value <= load_value;
    end
```

## Guidelines for power optimization (non-normative)
- Priority: Power optimization is important, but secundary to meeting timing constraints
- Reset: Avoid asynchronous reset whenever possible, follow "Reset Reduction" general guidelines
- Valid Bit: For output signals on a module that have a corresponding valid bit, clock a 0 into the register (if present) for the non-valid case **if timing allows**
    - Rationale: (non-valid) data changes add to Cdyn with no ROI
    - Example snippet below:
```verilog
always_ff @(posedge clk) begin
	valid_out <= data_valid;
	// Maps directly to the FF synchronous reset pin on most FPGAs
	data_out  <= data_valid ? data : '0;
end
```


## Testing strategy
- Create *at least* one testbench for each module
    - Consider splitting into a separate basic and comprehensive testbench
- For complex modules or modules with a large test space, create multiple testbenches, as they can simulate in parallel (ensure each has a unique `$dumpfile` name)
- Emit explicit `PASS` on success and `FAIL` / `$error` / `$fatal` on failure so `verilog_simulate()` can verify results (benches without assertions are reported as `INFO`)
- Use `verilog_configure_testbench()` to persistently associate testbenches with modules in `turbostar.json` (and set `hierarchical=true` on integration/top modules so their submodule testbenches are included by default)
- Create test cases for bugs: whenever possible, write a test that reproduces the bug first, then fix the RTL and confirm the test passes
- Use `verilog_simulate()` to run the testbenches for a given module

## Edit process
- Use the dedicated tools for the standard "edit - compile - test - commit" cycle:
  - `fs_replace_lines` / `fs_write_file` for editing files
  - `verilog_compile` for syntax checking and synthesis health (use `lint_only=true` for a fast first pass)
  - `verilog_simulate` for running the test cases
  - `verilog_pnr_timing` for validating timing optimizations (if appropriate)
  - `verilog_update_timing` to update timing annotations in the codebase
  - `git_add` / `git_commit` to create the git commit

## Tool usage
- On initial project setup, use `verilog_project_settings()` to configure basic project properties (target FPGA, device, package, top module, target frequency, and `sv_path="src"`); ask the user for any properties that are not known to you yet.
- If a signal is governed by a valid bit (example: `output[7:0]` and `output_valid`), use `vcd_signal_qualifier()` to declare this relationship to help the waveform tooling mask invalid cycles.
- Consider using `vcd_signal_labels()` to provide readable interpretations for non-enum signals where appropriate (example: `opcode` signals can label individual hex values with instruction names).
- The `vcd_wave()` / `vcd_compare()` / `vcd_search()` tools provide LLM-optimized views of the signals in the design from `.vcd` simulation waveforms (pass `instance="<inst_or_module>"` to `vcd_wave()` to automatically sample all input and output ports of a module), and `vcd_signal_to_code()` traces a waveform signal back to its RTL driver and consumers.
- Use `verilog_compile()` to compile a module (or the whole project) for syntax checking, synthesis, and place-and-route. It scopes checks to the target module's hierarchy, groups warnings by severity (correctness/synthesis warnings vs. style notes), flags critical synthesis hazards (combinational loops, inferred latches), and prints synthesis cell statistics while saving the full log to `tmp://synthesis.log`. `lint_only=true` runs just the fast Verilator pass.

## Timing optimization strategy
- Timing optimization works best when you consider both the structural view and the post-routing physical timing view:
  - `verilog_logic_depth` provides a structural view of LUT logic depth between flip-flops
  - `verilog_update_timing` annotates module port declarations in-place with `// [timing: ...]` comments so you have instant visibility into which module inputs and outputs carry deep combinational chains when instantiating or wiring modules
  - `verilog_pnr_timing` gives the critical path timing after routing, decomposing logic vs. wire delay and reporting fan-out and $F_{\max}$
- Often, the structural view provides the underlying architectural reason for the more fine-grained (local) post-routing timing bottlenecks, and it is generally beneficial to have the two views augment each other.
- If an optimization attempt does not work, revert to the previous state, but keep a document in git that you update after the revert to avoid repeating the same optimization over and over


## Learnings

### "Control-Set Homogeneity Beats Reset Stripping" (FPGA)
  Within a module, it is better for all flip-flops to share the same reset style (e.g. all always_ff @(posedge clk or posedge rst)) than to
  selectively strip reset from half of them. Keeping control sets identical allows NextPNR to pack flip-flops tightly into single physical
  tiles with zero inter-tile routing hops. This rule overrides other guidance that recommends not using rst - many real-design simulations
  have shown that it is more important to have homogeneity than to do the avoid-rst optimization (which is valid in the more general case).