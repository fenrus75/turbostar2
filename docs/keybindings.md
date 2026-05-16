# Turbostar Keybindings

This document lists all implemented keyboard shortcuts and key combinations in the Turbostar editor.

## Global Shortcuts

These keys are handled by the central dispatcher regardless of focus (unless overridden by a specific component).

| Key Combination | Action | Notes |
| :--- | :--- | :--- |
| `Alt+F` | Open File Menu | |
| `Alt+E` | Open Edit Menu | (Menu exists, content TBD) |
| `Alt+S` | Open Search Menu | (Menu exists, content TBD) |
| `Alt+H` | Open Help Menu | (Menu exists, content TBD) |

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
| `Ctrl+X` | Next Word | Moves cursor to the start of the next word. |
| `Ctrl+Z` | Previous Word | Moves cursor to the start of the previous word. |

## Editor Editing

Active when an editor window has focus.

| Key Combination | Action | Notes |
| :--- | :--- | :--- |
| `Space` to `~` | Insert Character | Printable ASCII characters (32-126). |
| `Backspace` | Delete Character | Deletes character before cursor. Joins lines at start of line. |
| `Ctrl+D` | Delete Character | Deletes character at cursor. Joins lines at end of line. |
| `Ctrl+W` | Delete Word Forward | Deletes from cursor to start of next word. |
| `Ctrl+O` | Delete Word Backward | Deletes from cursor to start of previous word. |
| `Ctrl+J` | Delete to EOL | Deletes from cursor to end of the current line. |
| `Alt+O` | Delete to BOL | Deletes from start of line to the cursor. |
| `Enter` | Split Line | Splits the current line and moves cursor to the next line. |

| `Ctrl+Y` | Delete Line | Removes the entire current line. |

## Block / Selection Commands (Ctrl+K Prefix)

These commands require pressing `Ctrl+K` first, followed by the command letter.

| Key Combination | Action | Notes |
| :--- | :--- | :--- |
| `^K B` | Set Selection Begin | Sets the start marker at the current cursor position. |
| `^K K` | Set Selection End | Sets the end marker at the current cursor position. |
| `^K C` | Copy Block | Copies selection to the current cursor position. |
| `^K M` | Move Block | Moves selection to the current cursor position. |
| `^K E` | Edit / Load File | Prompts for a filename to load into the editor. |
| `^K W` | Write / Save As | Prompts for a filename to save the current document. |
| `^K Q` | Quit / Abort | Exits the application without saving. |
| `^K X` | Save & Exit | Saves the current document and exits. |
| `^K U` | Top of File | Moves cursor to the beginning of the document. |
| `^K V` | End of File | Moves cursor to the end of the document. |
| `^K Y` | Delete Block | Deletes all text between start and end markers. |
| `^K H` | Hide/Clear Selection | Removes both start and end markers. |

## Quick / Search Commands (Ctrl+Q Prefix)

| Key Combination | Action | Notes |
| :--- | :--- | :--- |
| `^Q F` | Find Text | Opens the advanced Find dialog. |
| `^Q A` | Replace Text | Opens the advanced Replace dialog. |

## Search Commands

| Key Combination | Action | Notes |
| :--- | :--- | :--- |
| `^K F` | Find Text (Prompt) | Low-friction search prompt in the status bar. |
| `^Q F` | Find Text (Dialog) | Opens the advanced Find dialog. |
| `^Q A` | Replace Text | Opens the advanced Replace dialog. |
| `^L` | Find Next | Repeats the last search operation. |


