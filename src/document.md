# Document Buffer (`document`)

## Goals
The `document` class represents the in-memory text buffer of a file being viewed or edited in Turbostar. It provides:
1. High-performance row-based text storage using discrete `std::shared_ptr<line>` objects.
2. Low-latency cursor navigation, column snapping, tab stop expansions, and viewport scrolling.
3. Multi-level transactional undo and redo with intelligent typing keystroke coalescence.
4. Block selection, clipboard cutting/copying/pasting, and whole-line operations (Wordstar/Joe dialect shortcuts).
5. Asynchronous syntax highlighting coordination and language server semantic token overlays.
6. Forward and backward pattern search and replace using RE2 regular expressions or exact string matching.

## Architecture and Constraints
- **Modular Design**: To prevent a monolithic 3,000+ line implementation, `document` methods are separated into focused translation units:
  - `document.cpp`: File I/O (`load_from_file`, `save_to_file`), listener registrations, buffer lifecycle.
  - `document_edit.cpp`: Character insertion, backspace, newline handling, auto-indentation, quote/bracket auto-pairing.
  - `document_undo.cpp`: Action history stack, undo grouping (`action_group`), typing vs batch delete coalescing.
  - `document_selection.cpp`: Block selections, line range deletions, clipboard integration.
  - `document_nav.cpp`: Cursor positioning, tab conversions, line boundary navigation, paging.
  - `document_search.cpp`: Regex/literal search passes, match coordinate resolution.
  - `document_format.cpp`: Buffer reformatting and indentation alignment via external tools or internal logic.
  - `document_highlight.cpp`: Syntax token dispatch and LSP diagnostic range application.
- **Thread Safety**: Protected by `std::shared_mutex mutex_`. Read-only tools (such as LSP hover queries or agent line reads) acquire shared locks, while editing operations hold exclusive write locks.
- **Listener Notification**: Implements `document_listener` callbacks (`on_line_inserted`, `on_line_deleted`, `on_document_changed`). External systems like `codereview_manager` and `perf_manager` register as listeners to automatically adjust line annotations as the user inserts or deletes lines.

## Lessons Learned
- **Immutable Line Snapshots**: Storing lines as `std::shared_ptr<line>` allows asynchronous highlighting threads to hold references to line snapshots without causing data races or blocking the user's keystrokes.
- **Fine-Grained Undo Grouping**: Grouping successive typing characters together while isolating structural edits (such as batch agent file replacements or block pastes) ensures the undo stack remains intuitive for the user and prevents partial corruption during multi-line rollbacks.
- **Line-Drift Mitigation**: External line references (like crashdump frames or code review item locations) easily drift when lines are inserted above them. Notifying listeners of line insertions/deletions preserves alignment across long-running editing sessions.
- **Implicit Selection Boundaries**: In Wordstar/Joe editing paradigms, a user who sets block begin (^KB) and moves the cursor without explicitly pressing ^KK expects the block to span from the anchor to the current cursor position. Supporting implicit cursor boundaries and mouse drag selections in `has_selection()` and `get_selection_range_unlocked()` ensures operations like Write Block (^KW) correctly target the selected text rather than falling back to whole-document operations.
- **Search & Replace Match Advancing**: When repeating search after a replacement (`find_next` with `is_repeat = true`), if the previous match was consumed by `replace_current` (`last_match_len_chars_ == 0`), `cursor_x_` is already positioned immediately after the replacement text. Advancing `start_x++` blindly would skip adjacent or consecutive occurrences (e.g. `foofoo` -> `foo` to `bar`). Only step over a character when the match was unconsumed (`last_match_len_chars_ > 0`). Additionally, ensure interactive prompt passes preserve forward continuation from the cursor even when the initial search was scoped to entire file.
