# Multiline Edit Component (`ui_multiline_edit`)

## Goals
The `ui_multiline_edit` component provides an interactive, scrollable multiline text input area for dialogs, prompt input panes, and agent chat windows:
1. Dynamic soft wrapping of lines (`visual_line`) respecting word boundaries within the available display width.
2. Comprehensive text editing operations including UTF-8 character insertion, backspace, delete, newline splitting, tab expansion, and bracketed paste.
3. Bidirectional navigation including character movement (arrows), word hopping (Ctrl+Left / Ctrl+Right, Ctrl-Z / Ctrl-X), line boundaries (Home / End / Ctrl-A / Ctrl-E), and viewport scrolling.
4. Block selection, copy, cut, and clipboard operations (OSC 52 export).
5. Command history navigation (Up / Down arrows when at first/last visual line) and external document synchronization (`link_document`).

## Architecture and Constraints
- **Coordinate Mapping**: Converts between linear byte offsets in `buffer_` and wrapped 2D visual line coordinates (`pos_to_coord` and `coord_to_pos`).
- **Word Wrapping**: Automatically updates wrapped visual line representations whenever `buffer_` is modified or display dimensions change.
- **External Document Synchronization**: Supports bidirectional linking with an active `document` model via `document_listener` callbacks.
- **History Management**: Remembers command submissions by category (`history_id_`), preserving in-progress edits when navigating up and down history stacks.

## Lessons Learned
- **Separating Navigation from Destructive Actions**: Binding word-right navigation to Ctrl-X and Ctrl+Right requires careful differentiation between block cut (when selection is active) and word navigation. Standard cursor navigation keys (such as `KEY_CRIGHT` / `KEY_CLEFT`) must cleanly perform word hops without inadvertently cutting text.
- **Terminal Key Disambiguation**: Modern terminal emulators send escape sequences (`\033[1;5D`, `\033[1;5C`) for Ctrl+Arrow combinations. Translating these early to uniform key codes (`KEY_CLEFT`, `KEY_CRIGHT`) ensures consistent behavior across both full document editing and compact dialog multiline inputs.
