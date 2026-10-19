# Line Buffer (`line`)

## Goals
The `line` class (`src/line.h`, `src/line.cpp`) represents a single logical line in a document or UI multiline buffer:
1. **UTF-8 Character and Byte Navigation**: Maps between UTF-8 character indices, byte offsets, and terminal display column widths (handling multi-byte characters, full-width CJK ideographs, and tab expansions).
2. **In-Place Mutation**: Supports character insertion, single-character and backspace removal, line splitting, and line merging.
3. **Syntax Attribute Association**: Stores syntax highlighting attributes (`syntax_attribute`) aligned with UTF-8 character positions for terminal color rendering.
4. **Thread-Safe Snapshotting and Streaming**: Protects internal string buffers and attribute vectors via `std::shared_mutex mutex_`, allowing concurrent readers (highlighting loops, LSP hover, document serialization) alongside synchronized writers.

## Architecture and Constraints
- **Character vs. Byte vs. Display Column**:
  - `length_in_chars()`: Number of decoded UTF-8 codepoints.
  - `length_in_bytes()`: Raw UTF-8 string byte size (`text_.size()`).
  - `char_to_display_col(char_pos)` / `display_col_to_char_pos(display_col)`: Terminal column positions accounting for tab stops (`global_tab_width`) and wide glyphs.
- **Thread Safety**:
  - Read-only operations (`get_text()`, `append_to()`, `length_in_bytes()`, `length_in_chars()`, `char_to_byte_offset()`, etc.) acquire `std::shared_lock(mutex_)`.
  - Mutating operations (`set_text()`, `insert_at()`, `remove_at()`, `split_at()`, `merge()`) acquire `std::unique_lock(mutex_)`.
- **Zero-Copy Streaming**:
  - To prevent $O(N)$ heap allocation and temporary string churn during document serialization or file saves, `line::append_to(std::string &out)` directly appends the internal `text_` into caller-provided buffers while holding the reader lock.

## Lessons Learned
- **Lock-Guarded String Lifetime**: Returning raw `std::string_view` from `line::get_text()` is unsafe because the underlying buffer can reallocate if another thread mutates the line immediately after the read lock releases. `line::append_to(std::string &out)` allows direct copy into target buffers without intermediate temporary string allocations while preserving strict mutex ownership.
- **Accurate Display Column Mapping**: Terminal tab widths vary based on their current modulo column offset. Using tab expansions and CJK wide-character checks ensures cursor column snapping accurately matches ncurses rendering.
- **UTF-8 Word and Identifier Extraction**: `line::get_word_span_at(char_pos, extra)` safely converts `char_pos` to byte offset under `std::shared_lock(mutex_)` and expands bidirectionally along UTF-8 character boundaries using `utf8::is_alnum_at_byte(text_, byte, extra)`. Returning `word_span` with exact `[start_char_pos, end_char_pos)` columns avoids full-line heap allocations in `document` while preventing byte-vs-character offset skew when multi-byte Unicode precedes the cursor.
