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
- `formal/` for formal verification property modules (`*_props.sv`) and SymbiYosys (`.sby`) configs
- `wave/` for generated waveform outputs 
    - give each testbench a unique filename such as `$dumpfile("wave/dump_<tb>.vcd")` so parallel testbench runs do not collide, and add `wave/` to `.gitignore`. (*If your project prefers a different location for waveforms, change this one convention and adjust the `$dumpfile` strings accordingly.*)

## SystemVerilog guidelines
- Default: one module per `.sv` file (possible exception: very small local helper modules)
- Avoid using C++ keywords as module or signal names (e.g., `register`, `switch`, `class`, `default`, `template`), as they can break Verilator C++ code generation/simulation.
- Option: group related modules into subdirectories within `src/` for logical organization
- Use modern SystemVerilog best practices (within the subset supported by both `yosys` and `verilator`):
  - `always_ff` for flip-flops/registers
  - `always_comb` for all combinational logic (assign default values to all outputs at the top of `always_comb` blocks to avoid accidental inferred latches, which FPGA targets such as `synth_ice40` reject as errors)
  - favor `logic` over `reg` or `wire` for signals and wires; replace legacy `wire`/`reg` occurrences with `logic` when editing components (this is not a simple search/replace and needs careful consideration!)
  - use `typedef enum` for FSM states
  - use `unique case` for state machines as much as possible; mandatory for the FSM case
  - always define a `default:` case in case statements.
- For larger designs where the top levels are exclusively modules connected by wires, avoid adding direct logic
	- instead create new modules or integrate the logic into existing modules.
- Use the `_q` suffix when one signal is a one-cycle delayed version of another signal, `_q2` for two cycles and so on.
- Use the `_n` suffix convention for active-low signals
- Reset polarity: Pick one reset polarity (rst or rst_n) for the whole design
- When using tristates (`inout` / `'z`), detect multi-driver collisions in simulation with an assertion (supported cleanly by both `verilator --assert` and `yosys` without `` `ifndef SYNTHESIS ``):
```systemverilog
assign bus = en ? val : 'z;
always_comb begin
	if (en) begin
		assert (bus === val);
	end
end
```


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

## Formal verification readiness
- Keep `src/` strictly synthesizable RTL — place formal properties in separate `formal/<module>_props.sv` checker modules instead of `` `ifdef FORMAL `` blocks inside RTL files (avoids macro collisions with Verilator lint/sim and keeps Yosys/SBY clean). Read `system://verilog/formal_template_props.sv` (sequential/FSM/handshake modules) or `system://verilog/formal_comb_template_props.sv` (purely combinational modules, `depth=1`) for annotated `*_props.sv` starter templates.
- Attach property modules at elaboration time with SystemVerilog `bind` (supported by both `yosys` and `verilator`), which can also bind directly to internal signals.
- Structure each property module around four basics:
  1. **Start-up guard**: track `f_past_valid` (init `0`, set `1` on `posedge clk`) before using `$past(...)`.
  2. **Reset assumption**: constrain cycle 0 on the clock edge (`always_ff @(posedge clk) if (!f_past_valid) assume (rst);`, matching the design's reset polarity) — avoid `always_comb` for this, as Verilator evaluates `always_comb` at `t=0` before `initial` blocks drive `rst`.
  3. **Invariants (`assert`)**: check safety properties that must hold on every active cycle (e.g., `!(full && empty)`).
  4. **Reachability (`cover`)**: add `cover(...)` for every major transaction, state transition, and corner case (e.g., FIFO full/empty, back-to-back ops, reset recovery) to prove `assume(...)` statements are not accidentally over-constraining the design and making assertions pass vacuously.
- **FSMs**: Add a `cover (state == <STATE>)` for every valid enum state to prove all states are reachable, and `assert` that invalid/unused state encodings can never be reached (plus key illegal transitions if applicable).
- **Mutually exclusive signals (at-most-one / one-hot)**: Whenever two or more **inputs** are assumed mutually exclusive (at most one high at a time), constrain them with `assume` (e.g., `assume (!(a && b));` or `assume ($countones({a, b, c}) <= 1);` — which Verilator also checks as a live assertion during simulation). Whenever two or more **outputs** are explicitly or implicitly mutually exclusive, verify them with `assert` (e.g., `assert (!(full && empty));` or `assert ($countones({p0, p1, p2}) <= 1);`).
- **Assume-guarantee pairing**: Whenever you add an `assume` on a module's inputs (e.g., protocol sequencing, state ordering, or mutual exclusion), add a matching `assert` in the **producing** module's `formal/<producer>_props.sv` (or the parent module connecting them) to prove the upstream RTL actually guarantees that condition. Unpaired assumptions leave a hole between unit proofs and real integration behavior.
- **Running formal checks**: `verilog_simulate()` automatically compiles with `--assert`, includes `formal/*.sv` `bind` property modules, and (with `coverage=true`) reports formal `cover()` hit counts alongside RTL line/port coverage in `system://verilog/coverage.md`. Run `verilog_formal()` (or `turbostar-verilog formal`) for SymbiYosys bounded model checking, k-induction proofs, and reachability/vacuity checks — no need to invoke `verilator` or `sby` directly via shell. For multi-cycle modules that need non-default settings during whole-project runs (e.g., an iterative multiplier/divider), add a header pragma in `formal/<module>_props.sv`: `// formal: depth=30 timeout=120 solver=bitwuzla` (supported keys: `depth`, `timeout`, `solver`, `mode`; explicit tool arguments override file pragmas).
- **Verilator `--assert` portability**: Avoid nesting `assert(...)` inside `else if` chains or `unique`/`priority` branches (Verilator can reject nested assertions with `%Error-UNSUPPORTED: Assert not allowed under another assert`). For multi-branch checks, compute the expected value or boolean condition first (e.g., `exp_state = ...`) and place a single `assert (state == exp_state);` afterward.
- **Diagnostic messages on checks**: For non-trivial `assert`/`assume` checks, prefer adding an `else $error("...", sig1, sig2);` action block so Verilator prints live failing-edge signal values before stopping (SymbiYosys ignores the `else $error(...)` block).
- **Binding `typedef enum` internal signals (`read_slang`)**: Implicit `.*` binding fails in `read_slang` when an RTL `typedef enum` (e.g., `my_state_t state`) connects to a `logic [N:0] state` port in `*_props.sv`. Pass an explicit width cast matching the port width in the `bind` statement: `bind mod mod_props u_props (.*, .state(3'(state)));`.
- **Avoid SAT bit-blasting wide `*` / `/` in formal assertions**: A full 16×16→32 reference multiplier or divider inside a BMC/k-induction `assert` will time out SMT solvers. Instead, use formal `prove` + `cover` for FSM, handshake, packing, flag, and edge-case invariants (which complete in seconds), and verify full arithmetic bit-accuracy with a dedicated unit testbench.
- **Pin down every output port**: Audit every `output` port of a formally verified module and ask: *"If this output had the wrong value on any clock cycle (including reset and idle cycles), would at least one `assert` fail?"* Unchecked outputs are the most common source of formal blind spots. For wide arithmetic datapath outputs where bit-blasting a full reference model would time out SMT, pin down structural/protocol invariants (reset value, stability when invalid/idle, edge cases, handshake/flag consistency) in formal and verify full bit-accuracy in unit simulation.
- **Reset checks & `$past()` reset guards**:
  - Explicitly verify reset behavior on the cycle following reset (`if (f_past_valid && $past(rst)) assert (...);`).
  - When checking normal cycle-to-cycle transitions with `$past(...)`, guard with `if (f_past_valid && !rst && !$past(rst))` so `$past()` never samples across a reset boundary.
- **Use lightweight ghost state over deep `$past()`**: For multi-cycle protocols (pipelines, FIFOs, iterative units), maintain small auxiliary counters or shadow registers inside `*_props.sv` (e.g., `in_flight` transaction counts) rather than multi-cycle `$past(sig, N)` chains.
- **Handshake stability & liveness**: On `valid`/`ready` interfaces, verify (via `assert` for outputs, `assume` for inputs) that `valid` and payload signals stay unchanged (`sig == $past(sig)` — preferred over `$stable(sig)` for Verilator `--assert` portability) while `valid && !ready`, and bound internal busy/stall duration with a watchdog counter (`assert (busy_cnt < MAX_BUSY);`) to catch deadlocks in BMC.
- **Inductive strengthening for `prove` mode**: When bounded model checking passes but k-induction (`prove`) fails from an unreachable internal state, add helper `assert` invariants constraining internal pointers, counters, and state relationships.
- **2-state vs. symbolic initialization**: Verilator defaults to `0`-initialized registers while Yosys formal treats unreset registers as arbitrary symbolic values (`anyseq`). Never assert values on unreset datapath registers before valid data is latched, and use 2-state equality (`==`, `!=`) rather than `===`/`!==` in `*_props.sv`.
```systemverilog
// formal/fifo_props.sv
module fifo_props #(parameter WIDTH = 8, parameter DEPTH = 16) (
    input logic clk, input logic rst,
    input logic full, input logic empty,
    input logic [$clog2(DEPTH):0] count
);
    logic f_past_valid = 1'b0;
    always_ff @(posedge clk) begin
        if (!f_past_valid) assume (rst);
        f_past_valid <= 1'b1;
    end

    always_ff @(posedge clk) begin
        if (f_past_valid && $past(rst)) begin
            assert (empty && !full && count == '0);
        end
        if (f_past_valid && !rst && !$past(rst)) begin
            assert (!(full && empty));
            assert (count <= DEPTH);
            cover (full);
            cover ($past(full) && !full);
        end
    end
endmodule

bind fifo fifo_props #(.WIDTH(WIDTH), .DEPTH(DEPTH)) u_fifo_props (.*);
```

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
- Waveforms: Use `vcd_signal_qualifier()` to mask invalid bus cycles, `vcd_signal_labels()` to map opcodes/values to names, `vcd_wave(instance=...)` / `vcd_search()` / `vcd_compare()` to inspect traces, and `vcd_signal_to_code()` to trace nets to RTL drivers/sinks. Keep wildcard patterns in `vcd_labels.json` specific (e.g., `*reg_sel1`/`*reg_sel2` rather than `*reg_sel*`) so unrelated signals like `wr_sreg_sel` are not mislabeled.
- Long-running benchmarks (>50k cycles): Full `$dumpvars(0, ...)` produces >100 MB VCDs per run (multiplied by `.lastpass.vcd`/`.previous.vcd` snapshots); keep `$dumpfile`/`$dumpvars` on short unit/instruction tests and make it opt-in (e.g., via `$test$plusargs`) or signal-scoped for long end-to-end benchmarks.

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

### "Consolidate Duplicate Arithmetic/Carry Chains to Help Placement" (FPGA)
   Even when duplicate adders/subtractors or separate register-vs-immediate ALU branches are not individually on the top critical path, 
   consolidating them (muxing immediate operands at the input register, unifying `ADD`/`ADC`/`SUB`/`SBB`/`CMP` into a single nibble-split add/sub carry chain, 
   and deriving half-carry directly from the nibble carry bit) cuts `SB_CARRY` and `SB_LUT4` count dramatically. On FPGAs, shrinking the ALU footprint reduces 
   routing congestion and lets `nextpnr` place the remaining logic tighter, improving both $F_{\max}$ and area.

### "Sever `BRAM -> Datapath` with a Capture FF" (FPGA)
   Never feed synchronous BRAM `rdata` directly through operand muxes into an ALU or wide carry chain. Adding a dedicated 1-cycle capture flip-flop (`BRAM -> FF -> ALU`) breaks the `BRAM clock-to-out + routing + ALU` critical path and typically yields a larger $F_{\max}$ gain than local LUT tweaks.

### "Valid >= 1 Cycle Early -> Register It" (FPGA)
   Any combinational decode, sign-extension, or length cone (e.g., displacement, sign-extended immediate, instruction length, readiness strobe) that is already valid at least one cycle before downstream execution consumes it should be registered at the producer boundary (`0 <- Reg`). This shrinks downstream `In -> Reg` depth and gives `nextpnr` clean placement boundaries at zero extra cycle cost.

### "Invert Non-Zero Idle Outputs to Keep Reset-to-Zero Homogeneity" (FPGA)
   When a registered output must reset and idle to all-1s (e.g., an active-low or `2'b11` idle port address), store it inverted (`out_n` reset to `'0`, `assign out = ~out_n;`) so every flip-flop in the module shares the same synchronous reset-to-zero control set for tight tile packing.
