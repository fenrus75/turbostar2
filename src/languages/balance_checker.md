# `languages::balance_checker`

Centralizes syntactic delimiter/block balance checking across all file-editing tools (`fs_replace_content`, `fs_multi_replace_content`, `fs_replace_lines`).

## Goals & Constraints
- **Generic Default**: Provides a single C-like `{`/`}` brace balance helper for all brace-delimited languages (`mime::uses_brace_syntax`) so individual languages do not duplicate brace scanning.
- **Modular Language Dispatch**: Delegates to language-specific implementations under `src/languages/<lang>/` (starting with `languages::verilog::balance_checker`) so file-editing tools remain language-agnostic.
- **Zero False Positives on Pre-Existing Imbalance**: Only emits warnings for delimiter pairs that transition from balanced (`0`) before the edit to unbalanced (`!= 0`) after the edit.

## Lessons Learned
- Consolidating the brace checker here eliminated duplicated scanners between `fs_replace_engine.cpp` and `fs_replace_lines_entry.cpp`.
