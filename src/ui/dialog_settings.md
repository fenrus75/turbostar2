# Settings Dialogs (`dialog_settings`)

## Goals
The `dialog_settings` module provides multi-tab configuration dialogs for editor, workspace, run, and syntax color preferences:
1. `create_editor_settings_dialog`: Tabbed container managing "General & Code" (clang-format style, compile-on-save), "Language" (project primary language and dynamic standard versions), "Build & Run" (build system, build dir, main executable, run args), "LSP & Features" (clangd, auto-open errors, security display flags), and "Syntax Colors" (interactive palette picker).
2. `create_run_settings_dialog`: Run arguments and target configuration dialog.
3. `create_syntax_colors_dialog`: Standalone syntax highlight customization dialog.
4. Persistence integration (`apply_settings_from_dialog`, `apply_editor_settings_from_dialog`, `apply_run_settings_from_dialog`) interfacing with `config_manager` to store preferences globally (`~/.cache/turbostar/config.ini`) or per-project.

## Architecture and Constraints
- **Tabbed Layout**: Hosted within `ui_tabbed_container` with auto-calculated sidebar width based on tab title lengths.
- **Dynamic Dependent Forms**: The "Language" tab coordinates two linked groups: selecting a primary language dynamically repopulates standard version choices (`populate_language_standards`) and triggers container reflow (`ui_tabbed_container::flow()`).
- **Custom Version Fallbacks**: Supports arbitrary and future language standards via the "Custom Std:" textbox when "Other" or "Custom / Default" is selected.
- **Save Scopes**: Supports both "OK (Save Project)" targeting project cache root and "Save Global" targeting global user configuration.

## Lessons Learned
- **Cross-Tab Form Value Querying**: `ui_tabbed_container::get_value` queries across all tab pages regardless of which tab is currently active, allowing settings from inactive tabs to be safely applied upon dialog confirmation.
- **Dynamic Radio Choice Binding**: When dynamically replacing child choices inside a radio group during a parent callback, invoking `flow()` on the child group, enclosing group box, and the tabbed container ensures dimensions and coordinates update cleanly without visual overlap.
