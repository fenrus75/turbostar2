# verilog_compile_tool

## Goals & Constraints

- **Purpose**: Provides a unified compile and project-health check tool for SystemVerilog/Verilog designs, running up to three stages:
  1. **Verilator Lint (`--lint-only -Wall --Wno-fatal`)**: Fast syntax, port width, and lint verification.
  2. **Yosys Synthesis (`synth_ice40` / `synth_ecp5` / `synth` + `check` + `stat`)**: Checks synthesizability, combinational logic loops, undriven wires, inferred latches (`$dlatch`), and cell counts.
  3. **`nextpnr` Place-and-Route (single-seed `--seed 1`)**: Verifies physical pin constraints (`.pcf`), FPGA resource utilization (`ICESTORM_LC`, `SB_IO`, `ICESTORM_RAM`), and baseline $F_{\max}$ timing when compiling the top module.
- **Log Management (`output_file`, default `tmp://synthesis.log`)**: Because Yosys and `nextpnr` produce 500–3,000+ lines of raw logs, the full raw output is written to `output_file` (`tmp://synthesis.log` by default) while the tool response returns a structured diagnostic summary of errors, warnings, hazards, cell counts, and device utilization.

## Lessons Learned

- **Running Verilator with `-Wall --Wno-fatal` Ahead of Yosys**: Passing `-Wall --Wno-fatal` causes Verilator to emit all `%Warning-...` diagnostics while only exiting non-zero on actual `%Error` syntax/elaboration failures, allowing the tool to surface rich Verilator lint warnings alongside Yosys synthesis results in a single call.
