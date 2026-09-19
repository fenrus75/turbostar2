# Hexdump Tool & Validator (`hexdump`)

## Goals
The `hexdump` tool provides formatted hexadecimal inspection of binary and text files for LLM agents. In addition to standard offset, hex byte, and ASCII representation columns, it augments the output with structural annotations (such as TAR, ELF, PNG, and JPEG header segments) discovered by registered format highlighters.

## Architecture and Constraints
- **Two-Stage Validation Pipeline**:
  - `hexdump_validator`: Validates file accessibility through `file_security_manager`, parses numeric or hex-encoded offsets (e.g. `"0x1080"`), applies global parameter aliases (`length`, `bytes`, `num_bytes`, `byte_count` -> `size`; `start_offset` -> `offset`; `file_path`, `filename` -> `path`), and enforces default and maximum byte bounds (`size` defaults to 256 bytes, capped at 4096 bytes).
  - `hexdump_tool`: Reads binary data, invokes format highlighters from `hex_highlighter_registry`, and formats rows into 16-byte blocks with structural annotations.
- **Context Window Protection**:
  - Unbounded hex dumps can quickly overflow LLM context limits. Restricting the default size to 256 bytes with a maximum ceiling of 4096 bytes guarantees reasonable output size even on large binary files.

## Lessons Learned
- **Required Size Friction**: Demanding a mandatory `size` parameter from LLM agents caused frequent tool invocation failures when agents simply wanted to inspect the start of a file or check magic numbers. Making `size` optional with a default (256 bytes) dramatically improves ergonomics.
- **Global Byte Slicing Aliases**: Slicing byte ranges across binary tools is standardized in `docs/tools.md` Rule 4 (`offset` and `size`). By providing global aliases for `size` (`length`, `bytes`, `num_bytes`) and `offset` (`start_offset`) in the base `tool_validator`, all byte-oriented tools achieve uniform behavior without per-tool boilerplate.
