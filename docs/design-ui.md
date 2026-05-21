# UI Architecture and Design

Turbostar uses a hierarchical, object-oriented composite pattern for rendering UI elements such as dialogs, buttons, and text boxes. This enforces a strict separation of presentation and layout logic from the core editor state.

## Core Concepts

### Relative Coordinates
All UI elements define their `x` and `y` coordinates **relative to their parent container**. 
- This allows a complex widget (like a file selector) to be built once and placed anywhere inside a dialog.
- The `ui_container` is responsible for translating these relative coordinates into absolute screen coordinates before calling its children.

### Event Routing and Hit Testing
Elements consume `editor_event` objects directly. Mouse events within the `editor_event` always carry absolute screen coordinates (`ev.mouse_x`, `ev.mouse_y`).
Because children receive their own computed absolute coordinates (`abs_x`, `abs_y`) during the `handle_event` call, they can perform simple, foolproof hit-testing using the provided `contains_coordinate()` helper.

### Values and State
Many UI elements produce a semantic value (e.g., a checkbox returns "true" or "false", a textbox returns its string content). 
Elements implement `get_value(name)` returning a `std::optional<std::string>`. Containers recursively query their children, allowing the developer to easily extract the state of a complex dialog form by name.

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
- **`ui_radio_choice` / `ui_radiobutton_group`**: Mutually exclusive selection controls.
- **`ui_checkbox`**: Toggled boolean selection.
- **`ui_textbox`**: Input area with cursor management.
- **`ui_listbox`**: Scrollable, selectable list of text items.
