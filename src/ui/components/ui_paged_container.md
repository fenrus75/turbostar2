# Paged Wizard Container (`ui_paged_container`)

## Goals & Constraints
- Manages multi-step wizard dialogs with a sequence of pages (`pages_`) and a bottom navigation button bar (`<< Back`, `Cancel`, `Next >>` / `Finish`).
- Uses grow-only sizing across all pages so stepping forward and backward does not cause the enclosing dialog frame to jump in size.

## Lessons Learned
- **Reflow on Page Transition**: `set_current_page()` must invoke `this->flow()` after firing `page_entered_cb_` (which may dynamically populate controls on the newly entered page) and before querying `get_focusable_elements()`, ensuring the active page's layout and child coordinates are up to date immediately upon page entry.
