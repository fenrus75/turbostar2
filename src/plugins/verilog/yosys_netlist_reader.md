# yosys_netlist_reader

## Goals & Constraints
- Parses elaborated Yosys JSON netlists generated via `yosys ... prep -flatten; write_json netlist.json`.
- Extracts hierarchy netnames, bit IDs, source locations (`src`), and cell connections.
- Bridges the simulation trace domain (full signal names in VCD traces) to the hardware source code domain (`file.sv:line`).
- Token efficiency: emits compact, concise summaries for LLMs without bloated AST dumps or redundant clickable markdown anchors.
- Robust source line parsing: strips column numbers and converts multi-span ranges (e.g. `file.sv:24.9-31.12`) into concise line numbers (`file.sv:24-31` or `file.sv:24`).

## Lessons Learned
- When Yosys runs `prep -flatten`, it flattens module instances into the top module while preserving hierarchical net names (`cpu8008.A_reg.value`) and exact source line coordinates (`src`).
- Every wire connected across module boundaries shares identical bit IDs in its `bits` array, allowing instant lookup of all interconnected ports across the hierarchy without manually walking port maps.
- **RTL Source Prioritization over Techmap Templates**: In post-synthesis netlists, Yosys frequently annotates cells and nets with piped source locations (e.g. `/usr/share/yosys/ice40/cells_map.v:26.33-27.52|sv/alu/alu.sv:45.10-45.25`). When cleaning source locations, `yosys_netlist_reader` checks for and filters out internal techmap/cell library files (`cells_map.v`, `cells_sim.v`, `arith_map.v`, `brams_map.v`, `share/yosys`, `techmap.v`), ensuring the user's actionable RTL location is selected instead of vendor fabric templates.
