# vcd_signal_to_code_tool

## Goals & Constraints
- Bridges VCD simulation signals to RTL source code definitions, drivers, and consumers using elaborated Yosys JSON netlists.
- Emits token-efficient text for LLM agents without noisy markup anchors.
- Gracefully handles missing netlist files by searching common workspace locations (`build/netlist.json`, `netlist.json`) or offering actionable instructions.

## Lessons Learned
- Simulation signal names from testbenches often carry extra scope prefixes (e.g. `tb_main.uut.`) that are absent from the synthesized top-level netlist. Suffix matching handles this seamlessly.
