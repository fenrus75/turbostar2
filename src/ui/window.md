# Window Component (`window`)

## Goals
The `window` class represents a rectangular display frame within Turbostar rendering document buffers, terminal sessions, or tool panes. It provides:
1. Turbo Pascal style bordered presentation with window titles, close buttons, zoom toggles, and scrollbars.
2. Viewport calculation (`top_line_`, `left_column_`) mapping global document buffer coordinates to terminal display cells.
3. Event routing and input dispatch via dedicated per-window event queues (`window_queue_`).
4. Transient, window-scoped mouse selection handling (click, double click word, triple click paragraph, drag, release) and auto-scrolling.
5. Content rendering with multi-layer visual annotations including syntax tokens, diagnostic underlines, search matches, and selection highlighting.

## Architecture and Constraints
- **Subclassing & Polymorphism**: Acts as the base class for specialized UI containers such as `agent_window`, `code_review_window`, `crashdump_window`, `diff_window`, `hex_editor_window`, and `terminal_window`.
- **Decoupled Mouse Selection**: Maintains transient coordinates (`mouse_sel_start_char_`, `mouse_sel_start_line_`, `mouse_sel_end_char_`, `mouse_sel_end_line_`). Mouse selections are window-scoped and strictly isolated from the document's persistent block markers (`^KB`/`^KK`).
- **OSC 52 Clipboard Export**: On mouse release (or double/triple click), selected text is exported directly to the host system clipboard using ANSI OSC 52 escape sequences without modifying the document state.
- **Visual Precedence & Distinct Palettes**: In `draw_content()`, mouse selection uses a distinct Light Blue background (Color Pair 41/42) while block selection uses Cyan (Color Pair 8/13). When both exist and overlap, mouse selection visually takes precedence on overlapping cells.
- **Git Gutter Indicators & Ghost Line Numbers**:
  - **Left Window Border Markers**: Draws colored indicators along the window's left single-cell border (`║` bright green for additions, `║` bright yellow for modifications, `▸` bright red for deletions above). Performance profiling samples (`pct >= 1.0`) take precedence over diff markers when active.
  - **Right-Margin Ghost Line Numbers**: Subtle low-contrast line numbers rendered in idle margin whitespace on the right side of the window. Ghost line numbers stay pinned to the right window border during horizontal panning. Because ghost numbers are drawn right after background fills, document code text naturally draws on top and overwrites digits if code reaches the right edge.
  - **Modal Line Number Support (`gutter_number_mode`)**:
    - `git` (`[#Git]`): Displays Git HEAD line numbers; deleted lines appear as number gaps (e.g. 44 -> 47); added lines display subtle dots (`··`).
    - `file` (`[#File]`): Displays physical buffer line numbers (1, 2, 3...) matching compiler error diagnostics. Auto-selected upon `F4` next-error navigation.
    - `none` (`[#Off]`): Hides line numbers while retaining left-border diff markers.
  - **Live In-Memory Diffing (`get_effective_diff`)**: When a document is modified in memory, rather than waiting for file saving to disk, `window` executes an in-memory diff via `git_manager::compute_diff` comparing cached HEAD lines against current buffer lines. The calculation is memoized against the document revision counter (`revision_`), running in under 0.5ms and avoiding recomputation during cursor movement or scrolling.
- **Dismissal Semantics**: Any keypress (navigation, typing, command prefixes) or subsequent mouse click dismisses any active mouse selection.

## Lessons Learned
- **Separation of Mouse and Block Selections**: Allowing mouse drag gestures to overwrite document block markers caused severe UX regressions, destroying user-defined block markers during casual clicks and causing visual glitches when combining mouse selections with keyboard shortcuts. Maintaining mouse selections as purely transient, window-scoped state preserves document integrity.
- **Boundary Auto-Scrolling**: When the user drags the mouse above the top boundary or below the bottom boundary of the window, auto-scrolling viewport lines allows selections spanning beyond visible screen space.
- **Distinct Selection Color Schemes**: Rather than conflating mouse selection and block selection under a single highlight color or applying confusing XOR inversions, rendering mouse selection in Light Blue (Pair 41) and block selection in classic Cyan (Pair 8) with mouse selection precedence provides clear, unambiguous visual feedback to the user.
- **Progressive Element & Compound Scoped Mouse Selection**: Using `line::get_element_span_at()` with optional `scope_delimiter` provides a clean, progressive click hierarchy: double-click selects granular basic elements (individual words, whitespace runs, or single delimiters), while triple-click on scoped identifiers (e.g. `std::string` or `foo::bar`) selects the complete compound type via `scope_delimiter = "::"`, smoothly falling back to paragraph selection on lines without scoped tokens.
- **Ghost Line Number Z-Ordering & Occlusion**: Rather than stealing precious horizontal terminal columns with a dedicated line number gutter, drawing ghost line numbers into idle margin whitespace directly behind code text ensures code text naturally overwrites digits without clipping or horizontal reflow. Left-border diff markers ensure diff visibility even when code fills the entire horizontal width.
- **Hoisting LSP Queries from Inner Character Loop**: Querying `doc_->get_lsp_highlights()` and `doc_->get_lsp_diagnostics()` returned `std::vector` by value inside the inner per-character loop, causing ~6,000 vector heap allocations/deallocations per frame (~13% total CPU time). Hoisting these queries once per frame and pre-filtering the small subset overlapping each visible line reduced vector operations to a fraction of a percent.

