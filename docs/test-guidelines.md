# Turbostar E2E Testing Guidelines

Turbostar relies heavily on an End-to-End (E2E) testing framework (`tests/e2e/turbostar_runner.py`) to verify the functionality of the editor and its UI. 

To ensure our test suite remains fast, reliable, and immune to race conditions, all existing and new tests must adhere to the following guidelines.

## 1. Verify Content via `assert_content_is()`

**Do not use screen scraping for content verification.**
The method `assert_text_on_screen()` relies on exactly what is visible in the terminal viewport, which is highly fragile against window resizing, scrolling, and timing issues.

When verifying that the document content has been modified correctly (e.g., insertion, formatting, block deletion):
- Use `runner.assert_content_is('tests/data/your_golden_file.txt')`
- The `assert_content_is` helper leverages the editor's "Save As" functionality to save the current document state and compares it exactly with your reference file.
- If the files differ, it provides a unified diff in the test output for easy debugging.

*Exception*: Screen assertions (`assert_text_on_screen`) may be used *only* when specifically testing UI elements like dialog boxes or status bar messages that cannot be saved to a file.

## 2. Load Setup Data via `^KR` (Insert File)

**Do not type large blocks of code character by character.**
Using `runner.send_keys("void foo() {\n ...")` adds significant artificial execution time to the test suite and is prone to input buffer overruns.

When a test requires a substantial starting document:
1. Place the starting text in a file under `tests/data/`.
2. Use the `^KR` (Insert File) command to load the file into the editor instantly.
   ```python
   # Wait for the file dialog
   runner.send_keys('\x0b' + 'r') 
   time.sleep(0.5)
   
   # Type the path to your data file
   runner.send_keys('tests/data/my_starting_file.txt\n')
   time.sleep(0.5)
   ```

## 3. Avoid Hardcoded `time.sleep()` Where Possible

Minimize the use of `time.sleep()`. While it is currently necessary for certain UI dialog transitions, prefer waiting for specific deterministic state changes.
- The `assert_cursor_position()` and `assert_selection_is()` helpers have built-in timeout loops that automatically poll for the desired state and return early as soon as the state is reached.

## 4. Test Concurrency and LSP Isolation

Tests that launch background language servers (clangd) or compilers consume significant system resources. 
- By default, `TurbostarRunner` runs with the `--no-lsp` flag to disable clangd. Only enable LSP when specifically testing LSP features (`runner.start(use_lsp=True)`).
- Tests that enable `use_lsp=True` **must** be marked as non-parallel in `meson.build` using `is_parallel: false` to prevent CI flakiness due to resource contention.

## 5. Standard Helper Commands

Use established helper methods in `TurbostarRunner` instead of manually calculating coordinates or sending raw ANSI escape sequences whenever a helper exists:
- `runner.assert_cursor_position(y, x)` (1-based logical coordinates)
- `runner.assert_selection_is(start_y, start_x, end_y, end_x)`
- Future: A `send_ctrlk(command)` helper will be added to wrap the delay logic for `^K` prefixes.

## 6. Avoid Direct `assert` Statements

**Do not use bare `assert` statements directly within test functions.**
A direct `assert` in a test script is a strong signal that either an existing helper method in `TurbostarRunner` should be used, or a new helper should be created.
- **Why:** Helper methods are critical because they can encapsulate robust polling logic and generate rich, diagnostic error messages (e.g., printing both expected and actual data, or showing a diff) upon failure, without clobbering the main test logic with boilerplate.