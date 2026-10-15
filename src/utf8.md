# UTF-8 and String Formatting Utilities (`utf8`)

## Goals
The `utf8` namespace (`src/utf8.h`, `src/utf8.cpp`) provides foundational string inspection, sanitization, terminal display-width measurement, line wrapping, and identifier case-conversion utilities used across the editor UI and LLM agent subsystems.

## Architecture and Constraints
- **UTF-8 Sequence Handling**:
  - Computes character counts (`length`), terminal column widths (`display_width`), and byte/character offset translations (`char_to_byte_offset`, `byte_to_char_pos`) without throwing exceptions on malformed byte sequences.
  - Provides `is_valid_utf8` and `sanitize` to defend UI rendering and JSON serialization against invalid UTF-8 sequences or embedded NUL bytes.
- **Terminal Output Sanitization**:
  - `sanitize_terminal_output` strips ANSI CSI/OSC escape sequences (including incomplete trailing sequences) so terminal dumps remain safe for TUI and LLM consumption.
- **Identifier Case Conversion (`to_snake_case`, `to_PascalCase`, `to_camelCase`)**:
  - Converts identifier strings across the three primary naming conventions: Linux kernel style (`snake_case`), Google/Antigravity style (`PascalCase`), and Microsoft/Copilot style (`camelCase`).
  - Handles multi-letter uppercase acronyms (e.g., `HTMLParser` -> `html_parser`, `URL` -> `url`) and digit-to-uppercase transitions (`utf8Char` -> `utf8_char`).

## Lessons Learned
- **Incomplete Escape Sequence Stripping**: When truncating raw PTY streams, an ANSI escape sequence may be cut off mid-sequence at the end of the buffer. Stripping trailing partial escape sequences prevents leaking raw control prefixes into the UI.
- **Cross-Harness Identifier Normalization**: Different LLM harnesses train models on distinct parameter casing conventions (`start_line`, `StartLine`, `startLine`). Providing deterministic, round-trip case conversion helpers allows the tool validator and MCP server to bridge these conventions seamlessly.
- **Case Conversion Roundtrip Invariants & UTF-8 Boundaries**: In `to_snake_case`, checking `(prev >= 0x80)` ensures that multibyte UTF-8 characters followed by uppercase ASCII words (such as `CaféAuLait` -> `café_au_lait`) preserve word delimiters. In `to_PascalCase`, preserving word-boundary underscores when the subsequent character is non-capitalizable (e.g. `x86_64` -> `X86_64`, `step_1` -> `Step_1`, or emoji/CJK scripts) maintains idempotency under combinatorial case transformations.
