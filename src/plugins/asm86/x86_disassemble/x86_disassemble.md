# x86 Disassemble Tool & Validator (`x86_disassemble`)

## Goals
The `x86_disassemble` tool decodes x86/x86-64 machine instructions using Zydis into human-readable assembly tables. It supports both:
- **ELF / Binary File Mode (`path`)**: Disassembles functions (`symbol`) or byte ranges (`offset`, `size`) directly from an ELF or binary file, automatically resolving symbol offsets and sizes from `.symtab`, annotating relative branch offsets (`<+0x...>`), call targets (`<func>`), `.plt`/`.rela.plt` dynamic stubs (`<func@plt>`), global/static objects (`<obj>`), and `.rodata` string literals (`<str#N>` with a deduplicated `#### Referenced String Literals` table below).
- **Raw Bytes Mode (`data`)**: Disassembles hex or Base64 instruction snippets at an optional starting `address`.

## Constraints & Security Considerations
- **Sandbox Validation**: When `path` is supplied, read access is validated through `file_security_manager::validate_access` before opening the file, and file reads are bounded to 50MB with disassembly slices capped at 4096 bytes.
- **Control Character Filtering**: Symbol names are validated via `fs_utils::is_safe_for_ui` to reject control sequences.
- **Linux Default Syntax**: Defaults to AT&T syntax (`"att"`) to match `objdump -d` and `hexdump` conventions on Linux, while allowing `"intel"` when requested.

## Lessons Learned
- **Sharing ELF Disassembly Infrastructure with `hexdump`**: By exposing symbol/section lookup (`find_symbol_by_name`, `find_section_by_name`) and target operand resolution (`resolve_target_annotation`) on `elf_hex_highlighter`, `x86_disassemble` and `hexdump` share identical call/PLT/object/`.rodata` resolution logic without duplicating ELF parsing code.
- **Auto-Resolving Symbol Size**: When disassembling a named function (`symbol`), defaulting `size` to the symbol's `.symtab` `st_size` (when `size` is omitted) disassembles the exact function body without spilling into adjacent functions or requiring the caller to look up symbol byte lengths first.
- **Recursive Control-Flow Traversal + Bounded Gap-Fill**: Linear disassembly can misdecode inline data or padding bytes after an unconditional `jmp` or `ret` as a multi-byte instruction that eats the start of the next real basic block. By running a two-phase worklist decoder over a per-byte `covered` bitmap—first following reachable `next_address` and `alt_next_address` edges from entry/symbol points, and then filling uncovered gaps (`switch` jump-table blocks, exception landing pads) with Zydis buffer length clamped to the next `covered` byte—reachable basic blocks are anchored first and gaps can never overrun or desynchronize them.
