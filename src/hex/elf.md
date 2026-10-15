# ELF Hex Highlighter (`elf_hex_highlighter`)

## Overview & Goals
The `elf_hex_highlighter` class inspects and annotates raw ELF (Executable and Linkable Format) binary data for both 32-bit and 64-bit architectures, supporting both Little Endian (LSB) and Big Endian (MSB) byte orders. It performs structural semantic labeling for the hex editor and disassembles x86/x86-64 machine instructions in code sections when Zydis is available.

## Key Responsibilities
- **Format Recognition (`can_handle`)**: Validates ELF magic (`\x7fELF`) and minimum size requirement (at least 64 bytes).
- **Header Parsing (`parse`)**: Extracts ELF identification fields, program headers (Phdr), and section headers (Shdr).
- **Symbol & Function Resolution**: Locates string tables (`.shstrtab`, `.strtab`), parses symbol tables (`.symtab`), maps symbols to their file offsets, and demangles C++ symbols using `<cxxabi.h>`.
- **Interactive Highlighting (`get_info`)**: Maps any byte offset in the buffer to its semantic section, header field, or disassembled instruction.
- **Structural Summary (`get_structure_summary`)**: Renders markdown overview detailing headers, sections, and symbols.

## Constraints & Security Considerations
- **Untrusted Binary Input**: Binary data can be maliciously crafted, truncated, or contain circular/wrapping offsets designed to cause out-of-bounds reads, infinite loops, or integer overflows.
- **No Direct Memory Pointer Indexing Without Bounds Checking**: All multi-byte integer and string readers (`read_u16`, `read_u32`, `read_u64`, `read_string`) verify `offset <= data.size() && N <= data.size() - offset` before memory access to prevent overflow wrapping near `SIZE_MAX`.
- **Division-by-Zero Defense**: Validates that `e_phentsize > 0` and `e_shentsize > 0` before table indexing.
- **Disassembler Bounding**: Limits x86 disassembly scans to the smaller of the symbol boundary and `data.size()`.
- **Display Sanitization**: Control characters, newlines, and markdown table characters (`|`, `` ` ``) in section or symbol names are sanitized into printable equivalents.

## Lessons Learned
- **Bounds Checking Arithmetic Overflows**: Writing `if (offset + 4 > data.size())` can wrap around `size_t` when `offset` is close to `SIZE_MAX` (e.g. `(SIZE_MAX - 2) + 4 == 1`), which bypasses the check and causes out-of-bounds reads. Always use subtraction `if (offset > data.size() || 4 > data.size() - offset)`.
- **Table Size Multiplication Wrapping**: Multiplying `e_shnum * e_shentsize` can overflow on 32-bit platforms or wrap when added to `e_shoff`. Clamp the maximum valid entries to `(data.size() - e_shoff) / e_shentsize`.
- **Zero Entry Size in File Headers**: Malicious binaries can set `e_phentsize = 0` or `e_shentsize = 0` while declaring `e_phnum > 0`. Dividing by entry size without checking `> 0` triggers SIGFPE.
- **Clamping Highlight Info Range**: Callers expect `info.range_start + info.range_size <= data.size()`. If a section or header declares a size extending beyond the EOF of a truncated file, `get_info` must clamp `range_size = std::min(size, data.size() - range_start)` to protect caller operations like `data.subspan(info.range_start, info.range_size)`.
