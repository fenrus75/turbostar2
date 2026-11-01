# Verilog Plugin

## Overview and Goals
The Verilog plugin provides tooling for digital ASIC and FPGA hardware engineering within TurboStar. It integrates simulation analysis, waveform inspection, and hardware verification tools directly into the AI agent workflow under the `verilog` tool family.

Primary focus areas include:
1. **Waveform and Simulation Dump Inspection**: Parsing and querying Value Change Dump (`.vcd`) files produced by Verilog/SystemVerilog simulators (such as Verilator, Icarus Verilog, ModelSim, VCS, and QuestaSim) without requiring bulky external GUI viewers or blowing LLM context windows.
2. **Signal Hierarchy Discovery**: Inspecting module scopes, signal declarations, bit widths, and signal types.
3. **Transition and Value Analysis**: Querying signal states and transitions across specific simulation time windows or on specific clock edges.

## Architecture and Constraints
- **Dynamic Plugin Lifecycle**: Conforms to `docs/design-plugin.md` using C-linkage dynamic entry points (`plugin_name`, `plugin_description`, `plugin_run`, and `plugin_unload`).
- **Tool Family**: Registers the `verilog` tool family upon loading and cleanly unregisters upon unloading.
- **Resource and Memory Efficiency**: Simulation dumps can range from megabytes to gigabytes. Tools in this plugin must stream or index VCD files efficiently rather than buffering entire multi-gigabyte traces into memory all at once.
- **Security**: All file paths and untrusted user inputs must be validated against `tool_context::fs_security` sandboxing guidelines.

## Lessons Learned
- Tool families registered dynamically in plugins must also unregister in `plugin_unload()` to prevent dangling references in `agentlib::tool_registry` across reload cycles.
