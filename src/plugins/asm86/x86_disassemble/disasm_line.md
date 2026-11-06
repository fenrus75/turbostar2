# Disassembly Line IR (`disasm_line`)

## Overview & Goals
`disasm_line` (`src/plugins/asm86/x86_disassemble/disasm_line.{h,cpp}`) is the intermediate representation (IR) for a single logical entry in a disassembled machine code listing. Instead of streaming decoded bytes directly into formatted output strings, `x86_disassemble` decodes a byte range into a `std::vector<disasm_line>`, runs multi-pass control-flow and symbol enrichment over the sequence, and then renders the final listing.

## Key Responsibilities
- **Instruction Decoding & Control Flow (`decode_one`)**:
  - Wraps Zydis instruction decoding (`ZydisDisassembleATT` / `ZydisDisassembleIntel`) and stores `address`, `file_offset`, `length`, raw `bytes`, and shortened hex instruction text.
  - Populates explicit control-flow edges:
     - `next_address`: fallthrough instruction address (`std::nullopt` for non-fallthrough terminators such as unconditional `jmp`, `ret`, `ud2`, `hlt`, or non-returning calls recognized by `is_noreturn_call_target` which render a trailing `# does not return` comment).
    - `alt_next_address`: target virtual address for relative branches and calls (`Jcc`, `JMP`, `LOOP`, `CALL`).
    - `memory_target_address`: target virtual address for `RIP`/`EIP`-relative memory operands.
- **Line Type Priority & Deterministic Sorting (`disasm_line_type`, `operator<=>`)**:
  - Orders entries at the same `address` by `disasm_line_type` (`source_location` < `function_header` < `block_label` < `comment` < `instruction` < `data_byte`) and `sub_order`.
  - Allows synthetic entries (such as `# Function ...` header comments or `L1:` basic-block labels) to be appended in any pass with the target instruction's `address` and sorted into place immediately before the instruction via `std::ranges::stable_sort`.
- **Control-Flow & Call Target Operand Rewriting (`populate_control_flow_labels`)**:
  - Identifies intra-range branch targets from `alt_next_address`, assigns sequential `L1`, `L2`, ... labels in ascending address order, records incoming `branch_sources` on target instructions, rewrites branching instructions so `L<N>` is the primary operand (`jz L1 (0x... <+0x...>)`), rewrites resolved `call` instructions so `<sym>` is the primary operand (`call <sym> (0x...)`), and inserts `function_header` (`my_func:`) and `block_label` (`L1:`) rows before target instructions.
- **Batched DWARF Source Anchors with Codemap Range Header & Suffix Comments (`populate_source_locations`)**:
  - Resolves DWARF `file:line` transitions in batches via `turbostar::address_lookup::resolve_addresses`, probing the first 16 instructions and bailing out early if `>= 80%` return `"??"` to avoid wasted work on binaries without debug coverage.
  - Emits a single `# Function <name> at file <file> lines <from> - <to>` header comment above the function entry (resolving `<from> - <to>` via `codemap_fallbacks` / `find_enclosing_symbol` when the source file exists on disk), and attaches compact trailing `# line <N>` suffix comments (or `# <other_file>:<N>` when cross-file inlining switches files) to the instruction column whenever the source line transitions.

## Constraints & Security Considerations
- **Bounded Instruction Bytes**: Caps copied instruction bytes to `std::min(length, 15)` and `buffer.size() - offset` to prevent buffer overreads on truncated inputs.
- **Logical `indent_depth` Separated from Presentation (`format_markdown_row`)**: `indent_depth` is stored in abstract nesting levels (`0` for function headers and labels, `1` for top-level instructions, `2+` for nested loop/conditional bodies). At formatting time, every line shares the same fixed-width `Address` and `Bytes` columns on the left, while the `Code` column indents by `indent_depth * 2` spaces—aligning function (`my_func:`) and basic-block (`L1:`) labels at depth `0` of the code column and indenting instructions to the right of them.
- **Value-Type IR with Composite `(address, type, sub_order)` Key**: Storing `disasm_line` by value in a contiguous `std::vector<disasm_line>` and sorting by `(address, type, sub_order)` avoids per-instruction heap allocations while making multi-pass insertion of basic-block labels and source annotations trivial.
