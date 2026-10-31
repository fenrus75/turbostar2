# Markdown Syntax Highlighter (`markdown_highlighter`)

## Overview & Goals
The `markdown_highlighter` parses and styles CommonMark/GitHub-flavored Markdown documents in the text editor. It identifies level-1 headings, bold spans (`**bold**`), and bullet list markers (`- `, `* `).

## Key Responsibilities
- **File & Language Matching**: Recognizes `.md` and `.markdown` (case-insensitive) and languages `markdown`, `md`.
- **Heading Recognition**: Highlights lines starting with a single `#` as heading style.
- **Inline Styling**: Highlights bold spans (`**...**`) using CTRE compile-time regex matching.
- **List Markers**: Detects list items starting with `- ` or `* ` (optionally indented with spaces/tabs) and marks the list bullet character.

## Constraints & Security Considerations
- **Untrusted User Text**: Markdown text frequently contains emojis, multi-byte Unicode, and unmatched markdown syntax markers.
- **Character Slicing Bounds**: Maps substring byte positions to character counts via `utf8::length(text.substr(0, byte_pos))`.

## Lessons Learned
- **UTF-8 Alignment in Inline Spans**: Multi-byte characters (such as emojis like 🦀) inside or adjacent to bold markers change byte offsets relative to character offsets. Converting byte spans to Unicode character positions ensures attribute arrays never overflow or misalign attributes across character boundaries.
- **CTRE Migration**: Migrated bold span detection from `re2::RE2` to `ctre::search_all`, removing the Abseil dependency and eliminating runtime regex compilation overhead.
