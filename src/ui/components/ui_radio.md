# Radio Button Components (`ui_radio_choice` and `ui_radiobutton_group`)

## Goals
The `ui_radio` module provides mutually exclusive single-selection controls for forms, preferences, and wizards in TurboStar:
1. `ui_radio_choice`: Represents an individual selectable option displaying `( )` or `(*)`, supporting mnemonic hotkeys, mouse clicks, and keyboard focus.
2. `ui_radiobutton_group`: A container holding a collection of `ui_radio_choice` widgets, ensuring mutual exclusion across siblings and coordinating directional navigation (arrow keys).
3. Reactive selection change notifications (`set_on_selection_changed`) to support dependent UI updates (e.g., dynamically changing language standard versions when the chosen language changes).

## Architecture and Constraints
- **Mutual Exclusion**: When any child choice is selected (via click, Enter/Space, or hotkey), `ui_radiobutton_group::child_got_selected` marks that choice as selected and unselects all sibling choices.
- **Directional Navigation**: Within a group, `KEY_UP` / `KEY_LEFT` cycles to the previous option, and `KEY_DOWN` / `KEY_RIGHT` cycles to the next option, updating selection and focus simultaneously.
- **Dynamic Layout**: Supports both horizontal (`horizontal = true`) and vertical layouts with automatic natural width and height calculations.
- **Programmatic Control**: Supports querying selected choice by group name (`get_value`), setting selection programmatically (`select_choice`), and registering reactive callbacks (`set_on_selection_changed`).

## Lessons Learned
- **Selection State Consistency**: When calling `child_got_selected`, ensure the selected child is explicitly marked `selected_ = true` before notifying listeners. Otherwise, listeners responding to `on_selection_changed_` could inspect group or child state during the intermediate unselection loop.
- **Dynamic Repopulation**: When dynamically replacing child choices inside a radio button group (e.g. switching language standards), clearing and adding children requires a layout recalculation (`flow()`) on the group and its parent container to refresh positions and dimensions accurately.
- **Horizontal Stretch and Dynamic Width Expansion**: `ui_radiobutton_group` configured with auto-sizing (`want_stretch_ = true`) must return `want_stretch_` in `want_horizontal_stretch()` so enclosing containers (like `ui_group_box`) stretch the group across available inner columns. In `flow()`, vertical groups must expand `target_width` whenever `natural_width()` exceeds current `width_` to prevent longer options (such as `'IEEE 1800-2017'`) from truncating to shorter widths established by earlier choices (like `'C++23'`).
