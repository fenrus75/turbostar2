# Code Review Window (`code_review_window`)

## Goals
The `code_review_window` class (`src/ui/code_review_window.h`, `src/ui/code_review_window.cpp`) provides an interactive in-editor TUI window for inspecting, managing, and acting on automated and manual code review items:
1. **Split-Pane Review Presentation**: Renders a scrollable listbox of review items on the left side with status badges (`confirmed`, `disputed`, `invalid`, `resolved`, `verified-fixed`), accompanied by a detailed metadata and formatted diff/description pane on the right side.
2. **Keyboard and Mouse Driven Triage**: Supports single-key shortcuts and clickable bottom border actions to confirm (`C`), dispute (`D`), invalidate (`I`), resolve (`R`), verify (`V`), edit (`E`), comment (`A`), re-process (`P`), jump to source (`G`/Enter), or clear all items (`X`).
3. **Decoupled Event Dispatching**: Communicates user actions and source navigation requests back to the editor core via `event_queue` overloads without tightly coupling window UI state to file opening or dialog invocation logic.

## Architecture and Constraints
- **Subclass of `window`**: Inherits viewport management, frame borders, and per-window event queue processing.
- **Data Source**: Synchronizes with `codereview_manager::get_instance()`. Item status changes trigger synchronous project state serialization (`save_project()`) and emit `event_type::codereview_updated` back into `global_queue_`.
- **Source File Jump**: Resolves relative file paths against `project_manager::get_instance().get_project_root()` and pushes `event_type::open_file` carrying `abs_file:line_number` to focus the editor at the exact review hotspot.
- **Scroll Synchronization**: Preserves detail scroll offsets (`detail_scroll_offset_`) and resets offsets to 0 when the selected item changes in `ui_listbox`.

## Lessons Learned
- **Dedicated Event Queue Push Overloads**: Constructing local `editor_event` structs with manual field assignments (`ev.type`, `ev.key_code`, `ev.payload`) was verbose and error-prone. Migrating to dedicated `event_queue::push(type, key_code, payload)` overloads reduces boilerplate and standardizes event emission.
- **Switch-Based Key Dispatching**: Replacing long `if (key == ...) else if` cascades with a central `switch (key)` enables compiler verification of duplicate case labels, optimizes jump tables for clustered keyboard shortcuts, cleanly pairs case-insensitive character keys (`case 'c': case 'C':`), and routes unmatched inputs to `listbox_->handle_event()`.
