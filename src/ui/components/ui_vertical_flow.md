# Vertical Flow Container (`ui_vertical_flow`)

## Goals & Constraints
- Stacks child UI elements vertically from top to bottom with configurable horizontal padding (`x_offset_`), vertical padding (`y_offset_`), and inter-row spacing (`spacer_`).
- Automatically computes its `natural_width()` (`2 * x_offset_ + max_child_width`) and `natural_height()` from its children.

## Lessons Learned
- **Horizontal Stretch Must Respect `x_offset_` Padding**: Children that return `want_horizontal_stretch() == true` (such as `ui_radiobutton_group`, `ui_checkbox_group`, `ui_buttons_horizontal`, or centered `ui_text_label`) must still be placed at `target_x = x_offset_` and stretched across the inner content width (`effective_width - 2 * x_offset_`), rather than placed at `target_x = 0` with `total_width`. Placing stretched children at `x = 0` caused them to bleed into the container's left margin and overwrite parent dialog box borders (`║`).
