# HTML Syntax Highlighter (`html_highlighter`)

## Overview & Goals
The `html_highlighter` parses and colors HTML documents. It highlights tags, attributes, double-quoted and single-quoted attribute values, and comments (`<!-- ... -->`).

## Key Responsibilities
- **File & Language Matching**: Supports `.html`, `.htm` (case-insensitive) and language identifiers `html`, `htm`.
- **State Machine Highlighting**: Processes text using an explicit state machine (`normal`, `in_tag`, `in_double_quote`, `in_single_quote`, `in_comment`).
- **UTF-8 Stepping**: Steps through multi-byte UTF-8 sequences using `utf8::char_len` and maps byte offsets to character indices via `byte_to_char`.

## Constraints & Security Considerations
- **Untrusted HTML Payloads**: Input text may contain unclosed tags, truncated quotes, malformed comments, or mixed multi-byte Unicode.
- **Bounds Checking**: Every attribute assignment guards `char_pos < attrs.size()`.

## Lessons Learned
- **Multi-Byte Characters in Tags and Text**: Tag delimiters (`<`, `>`) and quotes (`"`, `'`) are ASCII, but text and attribute values can contain multi-byte UTF-8 characters. Advancing `i += char_len` ensures character boundaries are preserved.
