# Verilog PNR Timing Tool (`verilog_pnr_timing_tool`)

## Goals & Constraints

- **Purpose**: Implements the `verilog_pnr_timing` tool action, performing physical place-and-route timing analysis. Supports both pre-existing reports (`timing.json`) and automated on-demand synthesis + P&R execution.
- **Reg -> Reg Bottlenecks**: Focuses strictly on synchronous register-to-register paths determining $F_{\max}$, with clear delay attribution between logic depth and routing wire delay.
- **Graceful Guidance**: If target settings are missing, returns structured guidance explaining how the LLM can resolve the missing fields rather than failing abruptly.

## Lessons Learned

- **Filtering Testbenches**: When scanning workspace RTL for on-demand synthesis, automatically filtering out `tb_*.sv` and `*_tb.sv` testbench files prevents top-level duplicate module collisions in Yosys.
