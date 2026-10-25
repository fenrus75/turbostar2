# `inspector_window`

## Goals
- Provide an ambient, opt-in companion window docked at the bottom of the editor.
- Display live context about the symbol/call under the cursor (function prototype, synthesized argument proposals, declaration location).
- Respond seamlessly to cursor movement events without introducing GUI thread latency.
- Support graduated motion policies:
  - Freeze content during mild cursor movement (< 1s) to avoid visual distractions.
  - Blank the display during sustained aggressive scrolling (>= 1s) to prevent peripheral flicker.
  - Settle and update after cursor motion ceases for >= 250ms debounce time.

## Constraints
- Must not steal focus or keyboard input from active document editing windows (`bypass_global_shortcuts = true`).
- Height defaults to 4–6 lines scaling with terminal height, but is user-resizable.
- Asynchronous resolution via LSP; never blocks the rendering or main event loop.
- All drawing operations conform to Turbo Pascal 7 color scheme standards.

## Lessons Learned
- Coupling symbol inspection directly to the main loop causes multi-second latency spikes if symbols require network or cold LSP lookups. Emitting discrete `cursor_word_update` events from active editor windows and resolving uncached symbols in detached background workers keeps the UI at 0ms latency.
- Rapid cursor movement produces perceptual flickering if every word transition is immediately rendered; debouncing and graduated blanking preserves visual calm during code navigation.
