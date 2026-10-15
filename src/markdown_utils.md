# Markdown Utilities (`markdown_utils`)

## Goals
The `markdown_utils` namespace (`src/markdown_utils.h`, `src/markdown_utils.cpp`) provides text-processing tools to detect, tokenize, measure, and align Markdown tables across buffer contents and terminal views. It supports standard GitHub Flavored Markdown (GFM) pipe tables as well as UTF-8 box-drawing frames (`┌─┬─┐`), column wrapping, min/max width constraints, and formatting-aware display width calculations.

## Architecture and Constraints
- **Table Range Detection (`table_aligner::find_table_ranges`)**:
  - Distinguishes Markdown tables from arbitrary text containing pipes.
  - Requires contiguous rows matching `is_table_row` containing at least one valid delimiter row (`is_header_separator`, e.g. `|---|---|` or `|:---|:---:|---:|`).
- **Row Tokenization (`table_aligner::tokenize_row`)**:
  - Splits table rows into cells along unescaped pipe (`|`) boundaries.
  - Correctly preserves escaped pipes (`\|`) and backslashes (`\\`) within cell tokens so that Markdown source semantics remain valid after re-aligning.
  - Handles rows with or without outer pipes (`| col1 | col2 |` vs `col1 | col2`).
- **Formatted Display Width (`formatted_display_width`, `display_width`)**:
  - Measures visual terminal display columns using `utf8::display_width` (accounting for double-width emojis, CJK ideographs, and combining characters).
  - Discounts inline Markdown style tokens (e.g. `**bold**` discounts the four asterisks) while preserving their presence inside inline code blocks (`` `code **not bold**` ``).
- **Table Alignment & Layout (`table_aligner::align_table_block`)**:
  - Calculates maximum visual width for each column across all rows.
  - Expands columns evenly when `min_width` is set.
  - Truncates cells (with `...`) or word-wraps across sub-rows when `max_width` is set (`word_wrap = true`).
  - Renders either standard pipe tables or UTF-8 box-drawing frames (`use_utf8_frames = true`).
- **Document-Level Alignment (`align_all_tables`)**:
  - Scans full documents, extracts each table range in reverse order, aligns each table block, and splices the formatted results back into the document, preserving surrounding paragraphs, headers, and line endings (CRLF / LF).

## Lessons Learned
- **Preserving Escaped Pipes in Table Cells**: When tokenizing table rows, encountering `\|` must not strip the leading backslash. If the backslash is removed, subsequent formatting renders an unescaped pipe `|`, which corrupts the column count for Markdown parsers and breaks idempotency on subsequent formatting passes.
- **Outer Pipe Padding Dynamics**: When `use_outer_pipes = false`, the first and last columns still include inner padding spaces adjacent to their cell contents. Test assertions must account for this padding structure.
- **Inline Formatting Spans vs Code Spans**: When calculating cell display widths, markdown asterisks (`**`) should only be discounted when outside of backtick code spans (`` ` ``); code spans treat all enclosed characters literally.
