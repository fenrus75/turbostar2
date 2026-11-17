# `vcd_protocol_analyzer`

Coordinator and plugin registry for running `vcd_protocol_checker` instances on parsed VCD traces and formatting diagnostic reports.

## Goals & Constraints
- **Seamless Automatic Execution**: Designed to be invoked automatically by `verilog_simulate`, `vcd_file_info`, and `vcd_compare` without requiring a separate LLM tool.
- **Non-Intrusive**: Emits warnings and waveform inspection hints rather than overriding testbench `PASS`/`FAIL` exit status.

## Lessons Learned
- Initial implementation: registers `axi_stream`, `axi_lite`, and `axi4` variants by default.
