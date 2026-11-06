# Disassembly Line IR (`disasm_line`)

## Overview & Goals
`disasm_line` (`src/plugins/asm86/x86_disassemble/disasm_line.{h,cpp}`) is the intermediate representation (IR) for a single logical entry in a disassembled machine code listing. Instead of streaming decoded bytes directly into formatted output strings, `x86_disassemble` decodes a byte range into a `std::vector<disasm_line>`, runs multi-pass control-flow and symbol enrichment over the sequence, and then renders the final listing.

## Key Responsibilities
- **Instruction Decoding & Control Flow (`decode_one`)**:
  - Wraps Zydis instruction decoding (`ZydisDisassembleATT` / `ZydisDisassembleIntel`) and stores `address`, `file_offset`, `length`, raw `bytes`, and shortened hex instruction text.
  - Populates explicit control-flow edges:
    - `next_address`: fallthrough instruction address (`std::nullopt` for non-fallthrough terminators such as unconditional `jmp`, `ret`, `ud2`, `hlt`, or non-returning calls).
    - `alt_next_address`: target virtual address for relative branches and calls (`Jcc`, `JMP`, `LOOP`, `CALL`).
    - `memory_target_address`: target virtual address for `RIP`/`EIP`-relative memory operands.
- **Line Type Priority & Deterministic Sorting (`disasm_line_type`, `operator<=>`)**:
  - Orders entries at the same `address` by `disasm_line_type` (`function_header` < `source_location` < `block_label` < `comment` < `instruction` < `data_byte`) and `sub_order`.
  - Allows synthetic entries (such as `L1:` basic-block labels or DWARF source line transitions) to be appended in any pass with the target instruction's `address` and sorted into place immediately before the instruction via `std::ranges::stable_sort`.
- **Control-Flow Label Generation (`populate_control_flow_labels`)**:
  - Identifies intra-range branch targets from `alt_next_address`, assigns sequential `L1`, `L2`, ... labels in ascending address order, records incoming `branch_sources` on target instructions, annotates branching instructions, and inserts `block_label` rows before target instructions.
## Constraints & Security Considerations
- **Bounded Instruction Bytes**: Caps copied instruction bytes to `std::min(length, 15)` and `buffer.size() - offset` to prevent buffer overreads on truncated inputs.
- **Logical `indent_depth` Separated from Presentation (`format_markdown_row`)**: `indent_depth` is stored in abstract nesting levels (`0` for labels/headers, `1` for top-level instructions, `2+` for nested loop/conditional bodies). At formatting time, `Address` and `Bytes` columns remain vertically aligned at level 1 while levels `> 1` indent the `Instruction` column.

## Lessons Learned
- **Value-Type IR with Composite `(address, type, sub_order)` Key**: Storing `disasm_line` by value in a contiguous `std::vector<disasm_line>` and sorting by `(address, type, sub_order)` avoids per-instruction heap allocations while making multi-pass insertion of basic-block labels and source annotations trivial.
