# Project & File Dialogs (`dialog_project.cpp`)

## Goals & Constraints
- Implements the Turbo Pascal style File Open/Save dialog (`file_dialog_impl`), Code Review Edit dialog, Image Manager dialog, and the 3-step Create New Project wizard (`create_new_project_dialog`).

## Lessons Learned
- **Consistent Group Box Wrapping for Radio Groups**: Wrapping `ui_radiobutton_group` inside a `ui_group_box` on Step 2 ("Programming Language") of the New Project wizard matches Step 3 ("Build System" and "Language Standard") and provides the 1-column inner teal margin around radio button glyphs `(•)`.
