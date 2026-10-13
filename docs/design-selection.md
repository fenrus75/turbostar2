# Dual-Mode Selection Architecture & Specification

## 1. Overview & Problem Statement

Turbostar provides two distinct mechanisms for selecting text within a document:
1. **Mouse Selection** (transient, window-scoped, drag-and-copy workflow).
2. **Block Selection** (persistent, document-scoped, WordStar/Joe `^K` marker workflow).

### Historical Flaw (Commit `3b4eb2aa`)
In commit `3b4eb2aa`, mouse selection and block selection were conflated into a single set of coordinate fields in the `document` class. Furthermore, `has_selection_unlocked()` was modified to implicitly treat the current cursor position as an active selection boundary whenever only one marker was set. This introduced severe defects:
- Marking `^KB` alone immediately created an unwanted active selection between the marker and the cursor wherever the user navigated.
- Mouse drag interactions in `window.cpp` directly mutated `document::set_selection()`, destroying persistent block markers.
- In `window::draw_content`, a boolean XOR hack `(in_block_selection != in_mouse_selection)` was introduced to resolve coordinate collisions.

### Core Principle: Strict Decoupling
Mouse selection and block selection represent two fundamentally different user intents. They must have independent state lifecycles, independent ownership domains, and clearly defined interaction and rendering rules.

```
+-------------------------------------------------------------------------+
|                              Editor Window                              |
|                                                                         |
|  +---------------------------+       +-------------------------------+  |
|  |     Mouse Selection       |       |       Block Selection         |  |
|  | (Transient, Window-Owned) |       |   (Persistent, Document-Owned)|  |
|  +---------------------------+       +-------------------------------+  |
|  | - Mouse down -> drag -> up|       | - Set via ^KB (start), ^KK(end)| |
|  | - Auto-copies to system   |       | - Survives navigation/focus   |  |
|  |   clipboard (OSC 52)      |       | - Tracks text insert/deletes  |  |
|  | - Cleared on any keypress |       | - Operated on by ^K commands  |  |
|  | - Overrides display       |       |   (^KC, ^KM, ^KW, ^KY, ^KT)   |  |
|  +---------------------------+       +-------------------------------+  |
+-------------------------------------------------------------------------+
```

---

## 2. Mouse Selection Specification (Transient / Window-Scoped)

### 2.1 Ownership & Lifecycle
Mouse selection is purely an interactive UI affordance owned by `window` (`src/ui/window.h`). It **never** mutates document state or document block markers.

- **Mouse Down (`event_type::mouse_click`)**:
  - Anchors the starting coordinate `(mouse_sel_start_line_, mouse_sel_start_char_)`.
  - Sets `is_mouse_selecting_ = true`.
  - The initial anchor is modal to the active window.
- **Mouse Drag (`event_type::mouse_drag`)**:
  - While `is_mouse_selecting_ == true`, updates `(mouse_sel_end_line_, mouse_sel_end_char_)`.
  - **Auto-Scrolling**: If the mouse drag moves above the top border of the window (`mouse_y <= y_`), the window scrolls up; if dragged below the bottom border (`mouse_y >= y_ + height_ - 1`), the window scrolls down. This allows selecting ranges larger than the terminal screen.
- **Mouse Up (`event_type::mouse_release`)**:
  - Finalizes the mouse selection range.
  - Extracts the text between `(start_line, start_char)` and `(end_line, end_char)`.
  - Immediately copies the selected text to the system clipboard via ANSI OSC 52 (`ansi::copy_to_clipboard(selected_text)`).
  - Sets `is_mouse_selecting_ = false`.
- **Dismissal / Clearing**:
  - Any keypress event dispatched to the window immediately clears the mouse selection (`mouse_sel_start_line_ = -1`, `mouse_sel_end_line_ = -1`).
  - If a key is pressed while the mouse is down, it cancels the modal selection process immediately.
  - Clicking elsewhere with the mouse without dragging clears the previous mouse selection.

---

## 3. Block Selection Specification (Persistent / Document-Scoped)

### 3.1 Ownership & Markers
Block selection is owned strictly by the `document` class (`src/document.h`). It represents long-term text marking following the WordStar / Joe editor dialect.

- **Markers**:
  - `block_start_line_`, `block_start_char_` (set via `Ctrl+K B`).
  - `block_end_line_`, `block_end_char_` (set via `Ctrl+K K`).
- **Validity & Active State Rules**:
  - A block selection is active **if and only if** both the start marker AND the end marker are explicitly set, and `start < end` in document order:
    $$\text{has\_block\_selection}() \iff (\text{start} \neq \emptyset \land \text{end} \neq \emptyset \land \text{start} < \text{end})$$
  - **Incomplete / Half-Marker State**: If only `^KB` is set, or if only `^KK` is set, `has_block_selection()` returns `false`. Text is **not** highlighted as a selected range.
  - **Status Bar Feedback**: When only `^KB` is marked, the status bar displays a transient indicator (e.g. `[Block: Begin set at L:C; press ^KK to end]`) so the user has immediate feedback without premature text highlighting.
  - **Inversion & Equality**: If `^KK` is placed at the exact same location as `^KB`, or before `^KB`, the block selection is cleared (`block_start_ = -1`, `block_end_ = -1`).
  - **Explicit Clearing**: `Ctrl+K H` hides/clears both markers.

### 3.2 Marker Tracking on Text Edits
Unlike mouse selections, block markers are persistent and logically attached to the underlying content. As the document is edited, the markers automatically adjust:

1. **Edits Before Selection Start**:
   - If lines are inserted or deleted prior to `block_start_line_`, both `block_start_line_` and `block_end_line_` shift by the line delta.
   - If characters are inserted or deleted on `block_start_line_` before `block_start_char_`, `block_start_char_` shifts by the character delta. If `block_start_line_ == block_end_line_`, `block_end_char_` also shifts by the same amount.
2. **Edits Inside the Selection**:
   - If lines or characters are inserted or deleted between the start marker and the end marker, `block_start` remains stationary while `block_end` shifts with the edit, cleanly expanding or contracting the selection.
3. **Edits After Selection End**:
   - Edits occurring after `block_end` do not affect the markers.
4. **Degeneration**:
   - If edits cause the selection to become empty (`start == end`) or inverted (`end < start`), the selection is automatically cleared.
5. **Undo / Redo Integration**:
   - Snapshots of `block_start_*` and `block_end_*` are stored within `cursor_state` in each edit transaction (`edit_group`).
   - Undoing (`Ctrl+_`) or Redoing (`Ctrl+^`) restores the exact marker coordinates that existed prior to that edit.

---

## 4. Architectural Analysis: `document_listener` vs. Direct Tracking

During architectural evaluation, using the existing `document_listener` notification interface (`on_line_inserted`, `on_line_deleted`) was considered to update block markers. 

### Why `document_listener` is Unsuitable for Selection Tracking:
1. **Lack of Character-Level Precision**:
   - `document_listener` callbacks only supply line indices (`y`).
   - Per requirement 2.b, block markers require precise character-level tracking (`x`). If a user types or deletes characters on a partially selected line before or within the marker, `document_listener` receives no notification.
2. **Line Split Offset Ignorance**:
   - Pressing `Enter` in the middle of a line split creates a new line at a specific column offset `x`. `document_listener::on_line_inserted` does not know whether the split occurred before, inside, or after a marker on that line.
3. **Re-Entrancy & Mutex Deadlocks**:
   - `document_listener` notifications are fired synchronously while `document::mutex_` is held. Internal components of `document` listening to their own container introduce circular lock and re-entrancy hazards.
4. **Transaction Decoupling**:
   - Undo/Redo stores `cursor_state` containing marker coordinates directly in the undo stack. Direct tracking in `document::insert_text_unlocked` and `document::delete_text_unlocked` ensures that marker updates are $O(1)$, atomic, and perfectly synchronized with transaction recording.

**Conclusion**: Marker tracking belongs natively inside `document`'s core edit primitives (`insert_text_unlocked`, `delete_text_unlocked`, `split_line_unlocked`), while `document_listener` remains dedicated to coarse whole-line external observers (`codereview_manager`, `perf_manager`).

---

## 5. Block Commands (`Ctrl+K` Family)

All `Ctrl+K` block commands operate **exclusively** on the persistent document block selection. They ignore transient mouse selections.

| Shortcut | Command Name | Description |
| :--- | :--- | :--- |
| `^K B` | Set Selection Begin | Sets `block_start` marker at current cursor position. |
| `^K K` | Set Selection End | Sets `block_end` marker at current cursor position. If end <= start, clears markers. |
| `^K C` | Copy Block | Copies block selection to current cursor position (WordStar standard). |
| `^K M` | Move Block | Moves block selection to current cursor position. |
| `^K Y` | Delete Block | Deletes text in block selection and clears markers. |
| `^K W` | Write Block | Prompts for a filename to save only the block selection to disk. |
| `^K H` | Hide/Clear Block | Clears both block markers without deleting content. |
| `^K J` | Format Block | Formats text within the block selection using `clang-format`. |
| `^K T` | **Copy Block to Clipboard** | **(New Feature)** Copies the block selection directly to the system clipboard via ANSI OSC 52 with transient status bar confirmation. |
| `^K [` / `^K {` | Select Scope | Automatically sets block markers to enclosing `{}` braces. |
| `^K ]` | Expand Selection | Expands block markers to parent AST scope via LSP. |
| `^K Z` | Zap Whitespace | Strips trailing whitespace within the block selection. |

If any of `^K C`, `^K M`, `^K Y`, `^K W`, or `^K T` is executed when `has_block_selection()` is false, the editor emits a status warning (`"No block selected."`) and performs no mutation.

---

## 6. Rendering & Precedence Rules

Rendering occurs in `window::draw_content` ([src/ui/window.cpp](file:///home/arjan/git/turbostar2/src/ui/window.cpp)):

1. **Block Selection Rendering**:
   - When `doc_->has_block_selection()` is true, characters falling within `[block_start, block_end)` are drawn with the block selection attribute (Color Pair 8: White on Cyan, or Color Pair 2: White on Blue).
2. **Mouse Selection Rendering & Precedence**:
   - When `has_mouse_selection()` is true in the window, characters falling within the mouse selection range are highlighted.
   - **Precedence Rule**: If a character falls into both the block selection and the mouse selection, **mouse selection wins** and takes precedence. The user is actively interacting with the mouse selection, so its visual state dominates.
3. **No XOR Hacks**:
   - Character cell attribute computation:
     ```cpp
     bool in_block = has_block_sel && is_in_range(char_pos, block_start, block_end);
     bool in_mouse = has_mouse_sel && is_in_range(char_pos, mouse_start, mouse_end);
     
     if (in_mouse) {
         attr = COLOR_PAIR(MOUSE_SELECTION_PAIR);
     } else if (in_block) {
         attr = COLOR_PAIR(BLOCK_SELECTION_PAIR);
     }
     ```

---

## 7. Verification & Testing Strategy

1. **Unit Tests (`tests/unit/test_document_selection.cpp`)**:
   - Marker setting (`^KB`, `^KK`) and incomplete marker state verification (`has_block_selection() == false`).
   - Equality/inversion clearing (`^KK` before or equal to `^KB`).
   - Content insertion/deletion before, inside, and after markers.
   - Partial line character insertions/deletions shifting `start_char` and `end_char`.
   - Undo/Redo restoring exact marker positions.
2. **UI & Window Tests (`tests/unit/test_window_selection.cpp`)**:
   - Mouse down, drag, and release generating OSC 52 clipboard output.
   - Any keypress dismissing mouse selection without disturbing block markers.
   - Mouse auto-scroll on vertical boundary drag.
   - Rendering precedence: mouse selection overriding block selection.
3. **E2E Tests (`tests/e2e/test_selection_modes.py`)**:
   - `^KB` + navigation + `^KK` -> `^KT` copying block to clipboard.
   - `^KW` writing only block selection while mouse selection is active elsewhere.
