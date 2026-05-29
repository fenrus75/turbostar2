# UI Architecture and Design

Turbostar uses a hierarchical, object-oriented composite pattern for rendering UI elements such as dialogs, buttons, and text boxes. This enforces a strict separation of presentation and layout logic from the core editor state.

## Core Concepts

### Relative Coordinates
All UI elements define their `x` and `y` coordinates **relative to their parent container**. 
- This allows a complex widget (like a file selector) to be built once and placed anywhere inside a dialog.
- The `ui_container` is responsible for translating these relative coordinates into absolute screen coordinates before calling its children.

**⚠️ CRITICAL WARNING FOR CUSTOM UI ELEMENTS ⚠️**
When implementing the `draw(int abs_x, int abs_y)` or `handle_event(..., int abs_x, int abs_y)` methods for a custom UI element, the `abs_x` and `abs_y` parameters **already include** the element's relative `x_` and `y_` offsets. 
- **DO NOT** add your own `x_` and `y_` coordinates to `abs_x` or `abs_y` inside your implementation (e.g., `int start_x = abs_x + x_; // WRONG!`). 
- Doing so causes a double-translation coordinate bug, rendering your element twice as far down and to the right as intended. Always use the provided `abs_x` and `abs_y` directly as the top-left starting point for rendering and hit-testing.

### Event Routing and Hit Testing
Elements consume `editor_event` objects directly. Mouse events within the `editor_event` always carry absolute screen coordinates (`ev.mouse_x`, `ev.mouse_y`).
Because children receive their own computed absolute coordinates (`abs_x`, `abs_y`) during the `handle_event` call, they can perform simple, foolproof hit-testing using the provided `contains_coordinate()` helper.

### Overlay Rendering
To render popup widgets (such as dropdown option lists) on top of neighboring sibling controls in a container without being clipped or drawn over, containers execute a two-pass rendering cycle. Elements can override:
- `has_overlay()`: returns true if the element currently has an active overlay.
- `draw_overlay(int abs_x, int abs_y)`: draws the overlay content.
`ui_container::draw` draws all child elements in the first pass, and then draws all active child overlays in the second pass.

## Class Hierarchy

### `ui_element` (Abstract Base)
The foundation of all UI widgets.
- **State**: `name`, relative `x`, `y`, `width`, `height`, `has_focus`, and a pointer to its `parent`.
- **Key Methods**:
    - `virtual void draw(int abs_x, int abs_y) const = 0;`
    - `virtual bool handle_event(const editor_event& ev, int abs_x, int abs_y) = 0;`
    - `virtual std::optional<std::string> get_value(const std::string& target_name) const;`
    - `bool contains_coordinate(int target_x, int target_y, int my_abs_x, int my_abs_y) const;`

### `ui_container` (Composite Base)
Derived from `ui_element`. Manages a list of child elements.
- **Responsibilities**: 
    - Dispatches `draw` and `handle_event` to all children, dynamically computing their absolute coordinates.
    - Manages focus switching between children via `focus_next()` and `focus_previous()`.
    - Enforces that at most one child holds focus at any time.

### Concrete Elements (Examples)
- **`ui_button`**: An actionable control that can be clicked or triggered via hotkey.
- **`ui_radio_choice` / `ui_radiobutton_group`**: Mutually exclusive selection controls. A `ui_radio_choice` displays `(•)` when selected and `( )` when unselected. It uses `COLOR_PAIR(19)` for the indicator when focused and `COLOR_PAIR(17)` when unfocused. Its text label uses `COLOR_PAIR(17)` with `COLOR_PAIR(18)` highlighting the hotkey character. The `ui_radiobutton_group` acts as a logical container enforcing mutual exclusivity.
- **`ui_checkbox`**: Toggled boolean selection. It displays `[X]` when checked and `[ ]` when unchecked. Like radio buttons, the indicator uses `COLOR_PAIR(19)` when focused and `COLOR_PAIR(17)` when unfocused, while its text label uses `COLOR_PAIR(17)` with `COLOR_PAIR(18)` for the hotkey. Toggling is typically done via the spacebar or the assigned hotkey.
- **`ui_textbox`**: A single-line input area. Visually, it is rendered as a distinct solid block of background color (using `COLOR_PAIR(5)`) padded with spaces to fill its designated width. The text buffer is drawn over this background. A blinking or highlighted cursor indicates the active insertion point when the element has focus.
- **`ui_multiline_edit`**: A multi-line text input area. It supports automatic line-wrapping based on its width and internal vertical scrolling. It is primarily used for longer inputs like agent chat prompts. Pressing `Enter` invokes a submission callback.
- **`ui_listbox`**: Scrollable, selectable list of text items.
- **`ui_dropdown`**: A hybrid selector and text entry widget. It renders a single-line textbox with a right-aligned down arrow `▼` button. When opened, it uses the second-pass overlay rendering system to draw a framed pop-up menu list box directly below the input field. Users can navigate options with arrow keys and select with `Enter`, select via mouse clicks, or type directly to edit/filter.
- **`ui_fileselector`**: A highly specialized, complex composite element used for file browsing. 
    - **Visual Layout**: It presents a 2-column grid to display directories and files. The columns are separated by a vertical divider (`│`). The list area uses a `COLOR_PAIR(17)` (Black on Cyan) background, highlighting the currently selected item with `COLOR_PAIR(18)` (Bright Yellow on Cyan) when the view has focus. Below the list is a custom horizontal scrollbar using `◄ ░ ■ ►` characters.
    - **Scrolling Behavior**: Navigation is columnar. Up/Down arrows move the cursor vertically within a column. Left/Right arrows jump an entire column height (typically 7 items), effectively providing a purely horizontal paging experience through the directory contents.
    - **Linked Input**: The `ui_fileselector` is tightly coupled with a `ui_textbox` placed above it. As the user navigates through the file list, the text box's buffer is automatically synchronized with the name of the currently highlighted file or directory.
    - **Selection Action**: Pressing `Enter` while focused on the file list behaves contextually: if a directory is selected, the view descends into that directory and repopulates; if a file is selected, it triggers a confirmation action, acting similarly to pressing an "OK" button.

