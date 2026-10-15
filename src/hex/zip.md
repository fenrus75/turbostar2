# ZIP Hex Highlighter (`zip_hex_highlighter`)

## Overview & Goals
The `zip_hex_highlighter` class analyzes PKZIP archives in the hex editor. It handles both Central Directory (CD) oriented parsing (scanned backwards from the End of Central Directory record) and fallback sequential parsing of Local File Headers (LFH) when the EOCD is absent or damaged.

## Key Responsibilities
- **Magic Detection (`can_handle`)**: Matches `PK\x03\x04` at buffer offset 0.
- **EOCD & Central Directory Resolution (`parse`)**: Scans backwards up to 65,557 bytes for `PK\x05\x06`, decodes central directory offsets and sizes, parses CD headers (`PK\x01\x02`), and links them to local file headers.
- **Data Descriptor Handling**: Overwrites zeroed LFH compressed/uncompressed sizes with authoritative CD values.
- **Interactive Highlighting (`get_info`)**: Highlights LFH, compressed data payloads, CD entries, and EOCD records.
- **Structural Summary (`get_structure_summary`)**: Displays file listing table with offsets and compression statistics.

## Constraints & Security Considerations
- **Untrusted Archive Payloads**: Must defend against ZIP bombs (huge sizes), cyclic references, negative offset sign-extension, path traversal (`../../`), and corrupt length fields.
- **No Sign-Extension on 32-bit Shift**: Shifting a byte with the high bit set (`0x80 << 24`) in `int` produces a negative value that sign-extends to `0xFFFFFFFF80000000` when cast to `size_t`. Explicitly cast to `uint32_t` before bit-shifting.
- **Central Directory Bounds**: Validates `cd_offset <= data.size() && cd_size <= data.size() - cd_offset` using safe subtraction to prevent wrapping.
- **Sequential Fallback Bounds**: Verifies `cd.entry_size <= data.size() - offset` and `header_size + comp_size <= data.size() - offset` before advancing to guarantee loop termination and prevent infinite loops.
- **Filename Sanitization**: Sanitizes control characters, newlines, and markdown table characters (`|`, `` ` ``) in filenames.

## Lessons Learned
- **Bitwise Shift Integer Promotion & Sign Extension**: Shifting `uint8_t` promoted to signed `int` left by 24 sets the sign bit if byte $\ge 0x80$. When assigning to `size_t`, this sign-extends to an 18-exabyte number. Casting to `uint32_t` is mandatory for safe multi-byte integer decoding.
- **Sequential Fallback Progress**: Corrupted or overlapping ZIP records can cause `offset` to stall or wrap if entry lengths wrap `size_t`. Strict subtraction-based upper bound checks guarantee forward progress.
- **Highlight Range Clamping**: If an uncompressed or compressed file payload size exceeds the remaining buffer in truncated archives, `get_info` clamps `range_size = std::min(range_size, data.size() - range_start)` to maintain caller span-safety.
- **Dual-Mode Parsing Coverage**: Testing ZIP archives requires testing both the primary backwards-scanned EOCD / Central Directory parser as well as the forward sequential fallback parser that handles truncated or streamed archives where the EOCD record is missing or beyond the 65,557-byte reverse scan window.
