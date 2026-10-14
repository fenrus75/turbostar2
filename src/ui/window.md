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
- **Dismissal Semantics**: Any keypress (navigation, typing, command prefixes) or subsequent mouse click dismisses any active mouse selection.

## Lessons Learned
- **Separation of Mouse and Block Selections**: Allowing mouse drag gestures to overwrite document block markers caused severe UX regressions, destroying user-defined block markers during casual clicks and causing visual glitches when combining mouse selections with keyboard shortcuts. Maintaining mouse selections as purely transient, window-scoped state preserves document integrity.
- **Boundary Auto-Scrolling**: When the user drags the mouse above the top boundary or below the bottom boundary of the window, auto-scrolling viewport lines allows selections spanning beyond visible screen space.
- **Distinct Selection Color Schemes**: Rather than conflating mouse selection and block selection under a single highlight color or applying confusing XOR inversions, rendering mouse selection in Light Blue (Pair 41) and block selection in classic Cyan (Pair 8) with mouse selection precedence provides clear, unambiguous visual feedback to the user.
