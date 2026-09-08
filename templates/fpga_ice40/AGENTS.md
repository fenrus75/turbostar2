# @@PROJECT_NAME@@ Development Guidelines

## Project Overview
This project is an FPGA hardware design targeting the **Lattice iCE40-HX8K** (e.g. Alchitry Cu board) using the open-source FPGA toolchain (Yosys, nextpnr-ice40, Project IceStorm) and SystemVerilog.

## Build & Synthesis Instructions
- **Lint RTL**: `make lint` (runs `verilator --lint-only -Wall -sv src/top.sv`)
- **Simulate / Test**: `make test` (runs testbench verification)
- **Synthesize Netlist**: `make synth` (runs `yosys` to generate `synth/top.json`)
- **Place & Route**: `make pnr` (runs `nextpnr-ice40` using `pins.pcf` to generate `synth/top.asc`)
- **Generate Bitstream**: `make bitstream` (runs `icepack` to generate `synth/top.bin`)
- **Program Hardware**: `make flash` or `make program` (programs bitstream to SPI flash using `iceprog`)
- **Clean Build Outputs**: `make clean`

## Hardware & Pin Constraints
- Pin definitions are specified in `pins.pcf`.
- `clk`: 100 MHz clock oscillator on pin `P7`.
- `rst_n`: Active-low reset button on pin `P8`.
- `led[7:0]`: Onboard user LEDs on pins `J11`, `K11`, `K12`, `K14`, `L12`, `L14`, `M12`, `N14`.
- Expansion IO (IO Shield LEDs, DIP switches, pushbuttons, and 7-segment display) pinouts are documented in `pins.pcf` and `8segbits.txt`.

## Code Conventions
- Language Standard: @@LANGUAGE_STD@@
- Use `logic` for all internal nets and signals instead of legacy `reg` or `wire`.
- Use `always_comb` for combinational logic and `always_ff @(posedge clk or negedge rst_n)` for clocked registers.
- Keep RTL synthesizable; avoid delays (`#`) and unsynthesizable constructs in synthesizable modules (`src/top.sv`).
- Testbench files (`src/top_tb.sv`) may use delays, `$display`, and system tasks.
