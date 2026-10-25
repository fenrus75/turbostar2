# Turbostar Keybindings

This document lists all implemented keyboard shortcuts and key combinations in the Turbostar editor.

## Global Shortcuts

These keys are handled by the central dispatcher regardless of focus (unless overridden by a specific component).

| Key Combination | Action | Notes |
| :--- | :--- | :--- |
| `Alt+F` | Open File Menu | |
| `Alt+E` | Open Edit Menu | |
| `Alt+S` | Open Search Menu | |
| `Alt+T` | Open Tools Menu | |
| `Alt+P` | Open Options Menu | Preferences and settings. |
| `Alt+G` | Open Git Menu | Git integration commands. |
| `Alt+W` | Open Window Menu | |
| `Alt+H` | Open Help Menu | |
| `Alt+I` | Toggle Symbol Inspector | Toggles ambient companion window showing symbol context. |
| `Alt+1...9` | Switch Window | Directly activates the corresponding numbered window. |
| `F1` | Help | Open help dialog. |
| `F2` | Save | Smart save current document. |
| `F3` | Open | Open file dialog. |
| `F4` | Next Error | Jump to the next build error. |
| `F5` | Zoom Window | Maximize / restore current editor or terminal window. |
| `F6` | Terminal / Toggle Focus | Open terminal shell if none open, or cycle focus between terminal and editor windows. |
| `F7` | Next Hotspot | Jump to the next CPU performance hotspot line. |
| `F8` | Run Tests | Run the test suite using configured build system. |
| `F9` | Compile | Run the compile command using configured build system. |
| `F10` | Menu Bar | Focus top menu bar. |
| `Alt+F3`| Close Window | Closes the active window. |

## Menu Navigation

Active when the menu bar has focus.

| Key Combination | Action | Notes |
| :--- | :--- | :--- |
| `Esc` | Close Menu | Returns focus to the previous component. |
| `Up / Down` | Navigate Items | Selects the previous/next item in the current category. |
| `Left / Right` | Navigate Categories | Switches to the previous/next menu category. |
| `Enter` | Execute Item | Triggers the action of the selected menu item. |
| `[Letter]` | Item Hotkey | Executes the item with the corresponding red highlighted letter (e.g., `x` for Exit). |

## File Menu Items

Specific shortcuts defined within the File menu.

| Key Combination | Action | Menu Item |
| :--- | :--- | :--- |
| `F3` | Open... | |
| `F2` | Save | |
| `^Q S` | Save As | |
| `Alt+X` | Exit | Triggers application quit. |

## Editor Navigation

Active when an editor window has focus.

| Key Combination | Action | Notes |
| :--- | :--- | :--- |
| `Up` | Move Cursor Up | |
| `Down` | Move Cursor Down | |
| `Left` | Move Cursor Left | |
| `Right` | Move Cursor Right | |
| `Ctrl+A` | Beginning of Line | |
| `Ctrl+E` | End of Line | |
| `Ctrl+U` | Page Up | Moves cursor up by one page. |
| `Ctrl+V` | Page Down | Moves cursor down by one page. |
| `Ctrl+Right` / `Ctrl+X` | Next Word | Moves cursor to the start of the next word. |
| `Ctrl+Left` / `Ctrl+Z` | Previous Word | Moves cursor to the start of the previous word. |
| `Ctrl+G` | Matching Bracket | Jumps between matching `()`, `[]`, or `{}`. |

## Editor Editing

Active when an editor window has focus.

| Key Combination | Action | Notes |
| :--- | :--- | :--- |
| `Space` to `~` | Insert Character | Printable ASCII characters (32-126). |
| `Backspace` | Delete Character | Deletes character before cursor. Joins lines at start of line. |
| `Ctrl+D` | Multi-Cursor / Delete | Selects next occurrence of identifier in scope if on a word; otherwise deletes character at cursor. |
| `Ctrl+W` | Delete Word Forward | Deletes from cursor to start of next word. |
| `Ctrl+O` | Delete Word Backward | Deletes from cursor to start of previous word. |
| `Ctrl+J` | Delete to EOL | Deletes from cursor to end of the current line. |
| `Alt+O` | Delete to BOL | Deletes from start of line to the cursor. |
| `Enter` | Split Line | Splits the current line and moves cursor to the next line. |
| `Ctrl+Y` | Delete Line | Removes the entire current line. |
| `Ctrl+_` | Undo | Reverts the last logical edit operation. |
| `Ctrl+^` | Redo | Re-applies the last undone operation. |

## Multi-Cursor Editing

Multi-cursor editing allows concurrent manipulation of multiple occurrences of an identifier or token within the current semantic/syntactic scope.

| Key Combination | Action | Notes |
| :--- | :--- | :--- |
| `Ctrl+D` | Select Next Occurrence | Progressively selects the next occurrence of the word under cursor in current scope (wraps to start). When not on a word and no multi-cursor active, deletes character at cursor. |
| `Ctrl+Shift+D` / `^K D` | Select All in Scope | Instantly selects all occurrences of the word under cursor in the current function / scope. |
| `Ctrl+Shift+L` | Select All in Scope | VS Code compatible shortcut to select all occurrences of current token in scope. |
| `Esc` | Cancel Multi-Cursor | Clears all secondary cursors and returns to single caret mode. |
| `Left / Right` | Move All Carets | Collapses selections and steps all carets synchronously. |
| `Backspace / Delete` | Synchronous Deletion | Deletes character or selection across all carets simultaneously. |
| `Any Typing` | Synchronous Typing | Replaces selected tokens or inserts characters across all carets in a single undo transaction. |

## Block / Selection Commands (Ctrl+K Prefix)

These commands require pressing `Ctrl+K` first, followed by the command letter.

| Key Combination | Action | Notes |
| :--- | :--- | :--- |
| `^K B` | Set Selection Begin | Sets the start marker at the current cursor position. |
| `^K K` | Set Selection End | Sets the end marker at the current cursor position. |
| `^K C` | Copy Block | Copies selection to the current cursor position. |
| `^K M` | Move Block | Moves selection to the current cursor position. |
| `^K N` | New Window | Opens a new, empty window. |
| `^K P` | Compile File | Compiles only the currently active file using compile_commands.json. |
| `^K E` | Edit / Load File | Prompts for a filename to load into the editor. |
| `^K S` | Save File | Saves the current document. If untitled, acts as Save As. |
| `^K D` | Multi-Cursor (Select All) | Selects all occurrences of word under cursor in current scope (alias for Ctrl+Shift+D). |
| `^K A` | Save All | Saves all open, modified documents. |
| `^K W` | Write Block | Prompts for a filename to save the selected block to disk. |
| `^K R` | Insert File | Prompts for a filename to insert at the cursor position. |
| `^K Q` | Quit / Abort | Exits the application without saving. |
| `^K X` | Save & Exit | Saves the current document and exits. |
| `^K U` | Top of File | Moves cursor to the beginning of the document. |
| `^K V` | End of File | Moves cursor to the end of the document. |
| `^K G` | Next Error | Jumps to the next build error in the compilation output. |
| `^K L` | Go To Line | Prompts for a line number in the status bar to jump to. |
| `^K J` | Format Paragraph | Runs `clang-format` on the current block of text. |
| `^K T` | Copy Block to Clipboard | Copies block selection to system clipboard via OSC 52. |
| `^K [` / `^K {` | Select Scope | Automatically selects the current enclosing `{}` block. |
| `^K ]` | Expand Selection | Uses LSP to expand the selection to the next logical enclosing block. |
| `^K Y` | Delete Block | Deletes all text between start and end markers. |
| `^K H` | Hide/Clear Selection | Removes both start and end markers. |
| `^K Z` | Zap Trailing Whitespace | Trims trailing spaces and tabs from lines. Scoped to selection if active. |

## Quick / Search Commands (Ctrl+Q Prefix)

| Key Combination | Action | Notes |
| :--- | :--- | :--- |
| `^Q F` | Find Text | Opens the advanced Find dialog. |
| `^Q A` | Replace Text | Opens the advanced Replace dialog. |
| `^Q H` | Undo History | Opens the interactive Diff View for the document's undo stack. |
| `^Q S` / `^Q W` | Save As | Opens the Save File As dialog. |

## Search Commands

| Key Combination | Action | Notes |
| :--- | :--- | :--- |
| `^K F` | Find Text (Prompt) | Low-friction search prompt in the status bar. |
| `^Q F` | Find Text (Dialog) | Opens the advanced Find dialog. |
| `^Q A` | Replace Text | Opens the advanced Replace dialog. |
| `^L` | Find Next | Repeats the last search operation. |

## Git & Version Control Commands

| Key Combination | Action | Notes |
| :--- | :--- | :--- |
| `Alt+G A` | Git Add | Stages current file to git index. |
| `Alt+G R` | Git Refresh | Refreshes git branch and diff status. |
| `Alt+G N` | Toggle Line Numbers | Cycles line number display mode (`[#Git]`, `[#File]`, `[#Off]`). |
| Click `[#Git]` / `[#File]` / `[#Off]` | Cycle Line Number Mode | Top window border button cycles line number mode. |

## Mouse Interactions & Context Intelligence

TurboStar features an unconventional, intelligent Right Mouse Button (RMB) code action system that replaces generic edit menus with instant context-aware code generation and navigation:

| Mouse Action | Context | Behavior |
| :--- | :--- | :--- |
| `Right Click` (RMB) | Function Call Site | Opens intelligent Call Synthesizer popup. Provides 1–2 instant native C++ argument suggestions (`1`, `2`) based on in-scope variables and signature parameters, with support for existing argument biasing. Reserves Slot 3 (`3. [ ⌛ AI analyzing... ]`) for asynchronous background LLM completions without UI jumping. Also provides Jump to Definition (`D`) and Find References (`R`). |
| `Right Click` (RMB) | Type / Variable | Displays type synopsis header, Jump to Definition (`D`), Find References (`R`), and Format Code (`F`). |
| `Right Click` (RMB) | Text / Selection | Displays contextual Cut Block (`t` / `^KY`), Copy Block (`C` / `^KC`), Paste (`P` / `^KV`), Save (`S`), and Format Code (`F`). |
| `Left Click` (LMB) | Text Area | Positions cursor at clicked character and row. |
| `Left Click + Drag` | Text Area | Creates a mouse text selection. |
| `Double Click` (LMB) | Window Title Bar | Toggles between maximized and restored window bounds. |
| `Click [■]` | Window Title Bar | Closes the window (`^KQ` / Close). |
| `Click [≡]` | Window Title Bar | Opens the Borland window action popup menu. |
| `Click [#Git]/[#File]/[#Off]` | Window Title Bar | Cycles gutter line number display mode. |



