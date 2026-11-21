# `gridcad_scene_exporter`

Synthesizes SystemVerilog/Verilog modules with Yosys (`memory_collect`, `techmap`, `abc -lut 8`) and writes a `gridcad`-ready scene JSON.

## Goals & Constraints
- **Hierarchy & Word-Level Preservation**: Maps `$dff`/`$dffe`/`$adff`/`$adffe`/`$sdff`/`$sdffe` (`gridcad_reg`), `$mux` (`gridcad_mux`), `$pmux` (`gridcad_pmux`), `$add`/`$sub`, non-constant `$eq`/`$ne`, unsigned `$gt`/`$ge`/`$lt`/`$le`, bus bitwise (`$and`/`$or`/`$xor`/`$not`), bus reductions (`$reduce_*`), and `$mem_v2` (`gridcad_mem`) without `flatten`.
- **LUT8 & Consolidation**: Runs `abc -lut 8`, maps 1-input/2-input standard gates to `INV`/`BUF`/`AND2`/`OR2`/`XOR2`/`NAND2`/`NOR2`, consolidates exact- and subset-input `gridcad_lut`s into multi-output truth tables, and packs contiguous bus-sourced LUT inputs into vector pins (`A` or `A0..Ak`).
- **Bit-Exact Bus Wiring**: Synthesizes `gridcad_slice`, `gridcad_split`, and `gridcad_concat` for sub-range taps and concatenations so exported netlists have zero `driver: null` wires or disconnected components.
- **Rich Provenance & Project Identity**: Emits top-level `"project_dir"` and `"project_name"`, `"label_tables"` for `typedef enum` FSM states, `"vcd_signal"` on named wires, and `"src"` / `"src_verilog"` snippets.

## Lessons Learned
- `abc` requires a `BUF` cell in `gridcad.lib`, and Liberty cells may omit `port_directions` in `write_json` (fall back to `Y`/`Q` as outputs).
- Guarding `$eq`/`$ne` with `_TECHMAP_CONSTMSK_A_` / `_TECHMAP_CONSTMSK_B_` lets constant opcode comparisons fold into multi-output `gridcad_lut` decoders instead of exploding into dozens of comparator blocks.
- Packing contiguous bus bits on `gridcad_lut` inputs and consolidating full-bus 1-bit slices into `gridcad_split` dramatically reduces wire and component fanout.
