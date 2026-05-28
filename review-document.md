# Document.cpp Code Review

**File:** `src/document.cpp`

---

## Summary
The `document` class implements core document management, file I/O, undo/redo, and integration with various subsystems (git, LSP, highlighter). Overall the implementation is functional and follows the project's architectural split. However, several areas can be improved for correctness, thread‑safety, consistency with coding standards, and future maintainability.

---

## Issues & Observations

1. **Missing `set_modified()` after `insert_file`**
   - `insert_file` adds a block of lines but never marks the document as modified. Users may lose changes after a save because the `modified_` flag remains false.

2. **Thread‑safety of `modified_` flag**
   - `set_modified()` directly writes `modified_ = true` without acquiring `mutex_`. This can race with readers (`is_modified()`, `save()`) that hold a shared lock. The flag should be protected by the same mutex used for other state.

3. **Lock granularity during I/O**
   - Functions like `load_from_file` and `save` hold the document mutex while performing potentially slow file I/O. This can block other threads (e.g., UI refresh) unnecessarily. Consider unlocking before the actual I/O and re‑locking only for state updates.

4. **`apply_external_edits_json` – cursor adjustment logic**
   - The lambda `adjust_all` modifies cursor/selection coordinates while holding the same `mutex_`. The logic is fairly complex and could be split into a helper method. Also, the comment `// If the line we are on is deleted, move to previous or snap to start` is not fully implemented – the cursor `x` is set to `0` but `y` is unchanged, which may leave the cursor on a removed line.

5. **`insert_file` uses `std::vector<line>` but `insert_block` likely expects `std::vector<std::shared_ptr<line>>`**
   - The temporary `block` holds raw `line` objects, while the rest of the code stores lines as `std::shared_ptr<line>`. This discrepancy may cause compilation warnings or subtle bugs if `insert_block` expects shared pointers.

6. **Consistency of const‑correctness**
   - Several getters (`get_filename`, `get_safe_filename`) acquire a `shared_lock` and then return a reference to internal strings. The returned reference is valid as long as the document lives, but callers could modify the string via the reference (if they cast away `const`). Consider returning a copy or `const std::string&` with explicit `const`‑ness to avoid accidental mutation.

7. **Bounds checks**
   - Functions like `is_space_at_unlocked`, `notify_cursor_changed`, and the lambda inside `apply_external_edits_json` assume valid indices (`y`, `x`). Adding defensive checks (e.g., `if (y < 0 || y >= line_count_unlocked()) return false;`) would make the code more robust against corruption.

8. **Logging noise**
   - `has_nondefault_filename` logs every call, which can flood the log during normal operation (e.g., cursor movement). Consider lowering log level or removing the log.

9. **Formatting / Style**
   - Indentation uses tabs (as required) but some comment blocks lack a leading space after `//`. Ensure all comments follow the style guide.
   - Braces placement is correct per `.clang-format`, but a few long lines exceed the 140‑character limit (e.g., the lambda inside `apply_external_edits_json`). Split them for readability.

---

## Recommendations

| Area | Action |
|------|--------|
| **Modification flag** | Protect `modified_` with `mutex_` inside `set_modified()` and any other place that writes it (e.g., `insert_file`). |
| **Insert file** | After `insert_block(block);` call `set_modified();`. Also ensure `block` uses the same type as the rest of the code (`std::vector<std::shared_ptr<line>>`). |
| **I/O locking** | Refactor `load_from_file` and `save` to perform file opening/reading/writing outside the critical section. Keep only the state‑mutation part under lock. |
| **Cursor adjustment** | Extract the cursor/selection adjustment logic to a named helper method and fully handle the case where the line under the cursor is deleted (move cursor to the previous line or line start). |
| **Bounds safety** | Add explicit range checks in `is_space_at_unlocked`, `notify_cursor_changed`, and any place that indexes `lines_`. |
| **Logging** | Reduce verbosity of `has_nondefault_filename` or guard it with a debug‑only compile flag. |
| **Const‑correctness** | Return `std::string` copies from getters or mark the returned reference as `const` and document that it must not be mutated. |
| **Style** | Break long lambda lines, ensure comment spacing, and run `clang-format` after changes. |

---

## TODO (for next commit)
- [ ] Update `set_modified()` to lock `mutex_`.
- [ ] Add `set_modified();` call in `insert_file`.
- [ ] Change `insert_file` block type to `std::vector<std::shared_ptr<line>>` (or adapt `insert_block`).
- [ ] Refactor I/O sections to minimize lock hold time.
- [ ] Add helper `adjust_cursor_after_edit` and improve its logic.
- [ ] Add defensive index checks where missing.
- [ ] Reduce logging in `has_nondefault_filename`.
- [ ] Run `clang-format` on the file.

---

*This review was generated automatically. Please verify each recommendation against the project’s design and test suite before committing.*
