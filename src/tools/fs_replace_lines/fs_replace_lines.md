# File Line Replacement (`fs_replace_lines`)

## Goals
The `fs_replace_lines` tool enables surgical line-based file modifications. It supports both multi-edit batch operations (add, remove, replace) executed in descending order to avoid line-shifting offsets, and single-edit shortcuts (`start_line`, `end_line`, `new_content`) for rapid targeted updates.

## Architecture and Constraints
- **Two Usage Modes**:
  1. **Batch Multi-Edit**: Pass an `edits` array of `{line_number, type, original_text, replace_with}` objects sorted in descending line order.
  2. **Single-Edit Shortcut**: Pass top-level `start_line` (or `line_number`), optional `end_line`, and `new_content` (or `replace_with`). If `original_text` is omitted, the tool automatically reads the specified line range from disk to ensure safety verification.
- **Safety Verifications**:
  - Validates original text against target file lines with fuzzy offset tolerance (auto-adjusts for shifts within +/- 3 lines).
  - Brace balance checking (`check_brace_warnings`) alerts the model if an edit introduced unbalanced braces, with optional `strict` mode to automatically revert and reject changes that break syntax.
  - File drift tracking alerts when cumulative line shifts exceed thresholds, advising refreshing via `fs_read_lines`.

## Lessons Learned
- **Single-Edit Ergonomics**: In common coding sessions, agents frequently attempt to replace a single line or range of lines using direct parameters (`start_line`, `end_line`, `new_content`) rather than wrapping the request into a nested `edits` array with explicit `original_text`. Automatically synthesizing a single-edit operation and populating `original_text` from the file range eliminates frequent schema validation failures and streamline code edits.
