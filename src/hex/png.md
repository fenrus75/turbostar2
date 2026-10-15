# PNG Hex Highlighter (`png_hex_highlighter`)

## Overview & Goals
The `png_hex_highlighter` class parses and highlights Portable Network Graphics (PNG) images in the hex editor. It analyzes chunk sequences (IHDR, PLTE, IDAT, IEND, and ancillary chunks), extracts dimension and color metadata, and provides semantic highlighting for signatures, chunk lengths, types, data payloads, and CRCs.

## Key Responsibilities
- **Magic Validation (`can_handle`)**: Matches 8-byte PNG signature `\x89PNG\r\n\x1a\n`.
- **Chunk Enumeration (`parse`)**: Steps through chunks reading 4-byte length, 4-byte chunk type, payload, and 4-byte CRC.
- **IHDR Metadata Extraction**: Decodes image dimensions (width, height), bit depth, color type, compression, filter, and interlace methods.
- **Interactive Highlighting (`get_info`)**: Maps cursor offsets to specific fields within chunks (length field, chunk type, payload, CRC).
- **Structural Summary (`get_structure_summary`)**: Outputs metadata and chunk tables in markdown format.

## Constraints & Security Considerations
- **Untrusted Graphic Payloads**: PNGs from untrusted sources may declare excessive chunk lengths, contain truncated payloads, or embed malicious control characters in chunk types.
- **32-Bit Length Arithmetic Wrapping**: PNG chunk lengths are 32-bit unsigned integers. Adding the 12-byte header/CRC overhead must be computed as 64-bit `size_t` to prevent 32-bit integer overflow wrapping (e.g. `12 + 0xFFFFFFFE == 10`).
- **Sanitized Chunk Types**: Replaces non-printable characters, newlines, and markdown pipe/backtick characters in chunk types with `?` to prevent table corruption or UI breakout.
- **Out-of-Bounds Offsets**: Returns `normal` highlight info immediately when `offset >= data.size()`.

## Lessons Learned
- **32-Bit Arithmetic on Chunk Length Overhead**: Evaluating `offset += 12 + len` in 32-bit math wraps `0xFFFFFFFE` to `10`, advancing the parse offset into corrupted or forged chunks instead of halting. Calculating `static_cast<size_t>(len) + 12` and checking `total_chunk_len > data.size() - offset` eliminates this vulnerability.
- **Truncated Chunks & Clamped Ranges**: Declared chunk sizes that extend past buffer EOF must not parse subsequent chunks at invalid offsets. In `get_info`, all returned `highlight_info` ranges must clamp `range_size = std::min(range_size, data.size() - range_start)` to guarantee safe subspan slicing by callers.
