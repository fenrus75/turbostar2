# JPEG Hex Highlighter (`jpeg_hex_highlighter`)

## Overview & Goals
The `jpeg_hex_highlighter` class inspects and labels JPEG / JFIF image data for hex editing. It parses JPEG markers (SOI, APP0-APP15, DQT, DHT, SOF0-SOF15, SOS, DRI, RST0-RST7, EOI), extracts image metadata (dimensions, precision, color components, JFIF density), and maps entropy-coded scan data.

## Key Responsibilities
- **Format Recognition (`can_handle`)**: Detects SOI marker `\xFF\xD8`.
- **Marker Traversal (`parse`)**: Parses marker streams handling variable-length payloads, standalone markers (RST, EOI), padding floods (`0xFF` padding), and SOS entropy scan payloads with byte stuffing (`\xFF\x00`).
- **Interactive Highlighting (`get_info`)**: Maps offsets to specific marker headers, JFIF headers, or entropy scan data.
- **Structural Summary (`get_structure_summary`)**: Generates an overview of image properties and marker tables.

## Constraints & Security Considerations
- **Untrusted Input Defense**: Handles corrupt marker lengths, marker flooding, truncated scan streams, and non-conforming standalone markers.
- **Marker Length Validation**: By JPEG specification, variable-length markers include the 2 length bytes; any marker length `< 2` is invalid and must terminate parsing to prevent infinite loops.
- **Padding Floods**: Safely skips consecutive `0xFF` padding bytes without unbounded recursion or out-of-bounds pointer reads.
- **Accurate Payload Start**: Computes image payload offsets from the actual post-marker length position rather than assuming fixed single-byte padding.
- **Bounds Checking**: Enforces `offset < data.size()` in `get_info`.

## Lessons Learned
- **Zero-Length Variable Markers**: A variable-length marker declaring length 0 or 1 does not consume the length parameter itself. Validating `length_val >= 2` prevents offset stagnation and re-parsing.
- **Entropy Scan Data Bounds**: SOS entropy data extends until the next unescaped marker (or EOF). Byte stuffing (`0xFF 0x00`) and restart markers (`0xFF 0xD0..0xD7`) must be skipped without aborting scan boundaries prematurely.
- **Subtraction Bounds Checking & Clamped Ranges**: When parsing JFIF or SOF marker fields (10-18 bytes), check `pm.offset <= data.size() && data.size() - pm.offset >= N` rather than adding `pm.offset + N > data.size()`. In `get_info`, clamp `range_size = std::min(info.range_size, data.size() - info.range_start)` to protect callers.
- **Range Clamping Invariant**: In `clamp_and_return`, because any matching marker satisfies `offset >= info.range_start` and the caller establishes `offset < data.size()`, `info.range_start < data.size()` is guaranteed. Simplifying clamping logic eliminates unreachable defensive branches while maintaining bounds safety.
