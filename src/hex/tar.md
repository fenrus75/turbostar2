# TAR Hex Highlighter (`tar_hex_highlighter`)

## Overview & Goals
The `tar_hex_highlighter` class analyzes Tape Archive (TAR) files formatted according to the POSIX ustar standard. It parses 512-byte header blocks, extracts file paths (combining the 155-byte ustar prefix and 100-byte name), parses octal file sizes, calculates 512-byte padded boundaries, and identifies End of Archive markers.

## Key Responsibilities
- **Magic Detection (`can_handle`)**: Validates minimum size (512 bytes) and checks for `"ustar"` at offset 257.
- **Archive Traversal (`parse`)**: Steps through 512-byte blocks, reading filenames, ustar prefixes, and 12-byte octal sizes.
- **Padding Calculations**: Aligns file payloads to 512-byte block boundaries.
- **Interactive Highlighting (`get_info`)**: Maps cursor offsets to TAR headers, file data sections, padding zones, and End of Archive markers.
- **Structural Summary (`get_structure_summary`)**: Outputs file listings with offsets and sizes.

## Constraints & Security Considerations
- **Untrusted Archive Input**: TAR files may contain malformed octal size fields, non-null-terminated strings, huge sizes designed to cause integer overflows, or path traversal sequences.
- **Octal Arithmetic Overflow Defense**: Padded size calculation `(size + 511) / 512 * 512` guards against `size > SIZE_MAX - 511` to prevent overflow wrapping to small values.
- **Offset Bounds**: Verifies `512 + padded_size <= data.size() - offset` before advancing to ensure `offset` never wraps or creates infinite loops.
- **Non-null Terminated Strings**: Safely bounds string extraction to the fixed buffer slices (100 bytes for name, 155 bytes for prefix).
- **Control Character Sanitization**: Replaces control characters and table-breaking characters (`|`, `` ` ``) with safe printable equivalents.
- **Out-of-Bounds Queries**: Returns normal highlight info immediately when `offset >= data.size()`.

## Lessons Learned
- **Octal Size Arithmetic Wrapping**: Evaluating `size + 511` on huge sizes near `SIZE_MAX` causes integer overflow, collapsing `padded_size` to 0 or small values and causing the parser to treat raw payload data as subsequent tar headers. Bounding `size <= SIZE_MAX - 511` eliminates this vulnerability.
- **Non-Octal Character Rejection**: The 12-byte size field must strictly validate octal digits ('0'-'7') and ignore whitespace/nulls. Non-octal characters indicate corruption and must reset the size to 0.
- **Range Size Clamping & OOB Invariants**: In `get_info`, when `offset >= data.size()`, return `{hex_semantic_type::normal, "", 0, 0}` with `range_size = 0`. For headers, data, and padding, clamp `inf.range_size = std::min(inf.range_size, data.size() - inf.range_start)` to guarantee safe subspan slicing by callers.
- **Ustar Prefix and End-of-Archive Boundaries**: Verifying TAR parsing requires checking the 155-byte ustar prefix (byte 345) to ensure multi-directory paths are reconstructed correctly (`prefix/filename`), as well as semantic classification for zeroed End of Archive (EOD) trailing blocks.
