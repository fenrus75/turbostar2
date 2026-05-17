# short term items (fixes needed -- agents can automatically add todo items to this section)

- Update the LLM `tool_context` configuration in `agent_window.cpp` to dynamically determine the workspace root. It currently defaults to `std::filesystem::current_path()`, but it should query `git_manager` or similar to find the root of the active git tree if one exists.
- Implement a security scan for regular expressions provided by the LLM (e.g., in `fs_regexp_lines`) to detect and block malicious patterns (ReDoS attacks) before compilation.

# mid term items

- mouse support for the file dialog
   - the "recent files" drop thingy is the first candidate

# long term items   

- a git specific submenu when you click on the branch name in the title bar?
  only useful once we have more than git add implememented, so "long term". We should evaluate this as we add more git capabilities
    - git add
    - ...

- sandbox using systemd external tool calls like compile



# done items (move items here on completion)

## 17-05-2026
- Implemented `fs_write_file` tool to allow the LLM to create new files or completely overwrite existing ones. The tool takes a `force_overwrite` parameter (default false) to prevent accidental data loss. Furthermore, it explicitly queries the `document_provider` and will categorically reject any attempt to overwrite a file that the user currently has open in a live Turbostar buffer, avoiding race conditions and lost edits.
- Implemented `fs_compile_file` and `fs_compile_project` tools. These tools execute compilation commands synchronously (`popen`), cap the output at 10,000 characters to protect the LLM context window, and feed the output lines directly into `gcc_log_parser` and `build_error_manager` so the main UI workspace error list remains perfectly synchronized with the LLM's compilation attempts.
- Implemented `fs_compile_summary` tool. This zero-parameter tool aggregates workspace-wide diagnostics by querying `build_error_manager` and open documents via `document_provider`. It strictly reports only on files authorized by `file_security_manager` and returns a Markdown table showing counts of compiler and LSP errors/warnings per file.
- Implemented `fs_regexp_lines` tool and integrated `document_provider`. The tool uses `std::regex` to search files and returns results in a Markdown table. It reads from active editor buffers if the file is open, otherwise falls back to a safe disk read.
- Integrated LLM agent into the Turbostar UI. Created an `agent_window` (subclass of `window`) that features a multi-line input box and an auto-scrolling Markdown-highlighted `document` for chat history. LLM communication runs asynchronously on a background thread and synchronizes with the main UI via the central `event_queue`. Added "LLM URL" to the global settings dialog.
- Implemented `fs_read_lines` tool to allow the LLM to selectively read portions of files. Built using the Marshal Convention, it queries `file_security_manager` explicitly to safely resolve and bounds-check the path. It features fallback logic to `std::ifstream` for files up to 50MB with instant binary-rejection, while leaving a `document_provider` hook ready for integration with Turbostar's active UI buffers.
- Implemented `fs_list_dir` tool which lists directory contents as a Markdown table (Type, Size, Lines, Permissions). It enforces read access via `file_security_manager` and utilizes memory mapping (`mmap`) with fast binary skipping for high-performance line counting.
- Implemented `file_security_manager` and integrated it into `tool_context`. It automatically prevents directory traversal, resolves symlinks, enforces read/write permission tiers against allowed workspace roots, and filters paths using `.agentignore` patterns.
- Implemented enhanced two-stage security infrastructure for LLM tools based on `docs/llmtools.md`. Tools now self-register and strictly separate argument validation (Stage 1 via `tool_validator`) from execution and runtime validation (Stage 2 via `llm_tool` and `tool_context`).
- Implemented `llm_transport` abstraction for the LLM client, allowing seamless injection of `httplib_transport` (real network), `recording_transport` (traffic logging), and `replay_transport` (deterministic, network-less testing).
- Implemented `src/agentlib/` and `src/agentcli/` as the foundational LLM backend.
  - Phased plan 1-9 completed: Built an OpenAI-compatible client (`llm_client`) using `cpp-httplib` and `nlohmann_json`.
  - Implemented `tool_registry` for dynamic tool-call schemas and C++ callbacks.
  - Note: The infrastructure correctly formats and parses the JSON payloads. With a capable model (like Qwen3.5-9B-GGUF), the LLM naturally elects to use the tool, the C++ backend executes the callback, and the result is seamlessly injected back into the conversation (Phases 7-9 fully validated).
- Optimized LSP shutdown by removing the 550ms delay and using `SIGKILL` to ensure immediate process termination on exit.
- the ^KX dialog defaults to exit, but on hitting "enter" we should default to save-all
- Changed the default selection in the ^KX Force Quit dialog from "Exit" to "Save All" to prevent accidental data loss.
- Updated the LSP `clangd_manager::stop()` sequence to send a graceful `Shutdown` request (with timeout) and an `Exit` notification before forcefully terminating the process.
- Added standard key constants (e.g., `KEY_ESC`, `KEY_UP`, `KEY_CTRL_A`) to the test framework and refactored all E2E tests to use them instead of raw hex values for better readability.
- Fixed test suite flakiness by configuring heavy LSP tests (`test_lsp_selection` and `test_format_paragraph`) to run sequentially (`is_parallel: false`) and adding proper teardown delays to let `clangd` exit gracefully without impacting subsequent tests.
- Split `src/editor_events.cpp` into 9 specialized event handlers based on event_type to improve maintainability and compile times.
- Updated File menu hints to display native Turbostar ^K shortcuts (e.g., ^KS for Save) instead of function keys.
- Fixed hidden cursor bug when closing a menu by clicking outside of it.
- Fixed window close logic so closing the last window with the mouse correctly exits the app (or prompts) instead of spawning untitled.txt.
- Separated standard quit and force quit (^KX) logic and added auto-closing countdown to the force quit dialog.
- Reorganized the window title bar: Grouped Git branch and dirty status `[branch ✎]` on the left. Moved the popup menu button `[≡]` to the far right.
- test suite performance. We have lots of sleeps in the test suite and framework to let the editor keep up -- we could consider having turbostar give some
   indicator in the output for it being done with event processing -- that way we could short-circuit those sleeps.
   likewise, some "sleep + wait for event" patterns could become "wait for event with timeout" patterns (this is a simpler step than the feedback one)
- now that we have mouse support we can add magic buttons at the top title bar of windows
    - example: the git modified picture we have -- we could make it so that if you click that, you git add the file  
    - if we have the info to compile the file we could find some visual item to put somewhere that you can click to compile this file only
- src/document.cpp is very large, we may want to split this into a few files
  - Split `src/document.cpp` into 7 smaller sub-modules (edit, format, highlight, nav, search, selection, undo).
- Test Suite Improvements:
  - Created `docs/test-guidelines.md`.
  - Added `send_ctrlk` helper.
  - Transitioned screen scraping tests to `assert_content_is`.
  - Converted slow typing tests to use `^KR` (insert_file) with `tests/data/` files.
- there is a clear() in editor.cpp around line 450. This is causing flickering in the UI and is extremely annoying for the user, to the point that the application is unusable.
  - Removed `clear()` from `editor::render()`. Transferred `test_block_delete` and `test_scope_selection` to use `assert_content_is` instead of screen scraping to fix test flakiness.
- in the config system, make focus_idx_ an enum so that we don't need to renumber everything every time.
  - Refactored `focus_idx_` in both `settings_dialog` and `find_dialog` to use strongly-typed `enum class` constructs.
- we need to split up the event handling code at some point so that large
  events become their own methods -- the function is getting unwieldy
  - Splitted `editor::dispatch` into specialized `dispatch_event_<name>` handlers.



## 16-05-2026
- option for "compile this file only"
   - Implemented using compile_commands.json database to execute exact compiler command.
   - Added preference for "compile-on-save" (off by default) in settings.

- mouse support
   - for the window close button thingy in the left top
   - for menus


- Parse standard gcc/g++ error and warnings strings to feed back into our coloring, and add a "go to error" option (`F4` / `^K G`) that moves the cursor and view to the exact error.
   - Colored the whole horizontal line with an error red (and yellow for warning).
   - Disabled auto-scroll ("strip until N") in the compile window when F4 is hit to preserve original compiler messages.
   - Implemented a "Save All" feature (`^K A`).

- add a compile output window (implemented generic process_runner and split screen window)
   - Auto-activate (foreground) the output window when a build or test starts.
   - Give the output window a distinct background color (White on Black) to distinguish it from editable code.

- support for LSP servers (clangd)
   - Integrated leon-bckl/lsp-framework as a Meson subproject
   - Implemented hover information in the status bar (with word-based debounce)
   - Implemented Expand Selection (^K]) using selectionRange
   - Implemented live diagnostics highlighting (errors in red, warnings in yellow)
   - Implemented documentHighlight to show all occurrences of the variable/symbol under cursor
   - Restricted clangd to C/C++ file extensions (.cpp, .c, .h, .hpp)
   - Added `--no-lsp` command line flag and updated E2E test runner to use it by default.
   - Added persistent "Enable LSP" toggle in the Preferences dialog.

- improve syntax highlighting
   - multiple languages support (first one: markdown)
   - we will need an abstraction between the syntax highlighting thread and the language, one class per language most likely
   - each class should have a method for "is this filename for me" that returns a bool - the first one to say "yes" wins 
   - need to reevaluate this on "Save As" as the filename changes 
   - need to standardize between languages what the attributes mean, some sort of C++ enum equivalent
   - need to build it so that we can, over time, get to the LSP server approach

- better git integration: key decision: libgit(2) or exec to git? instinct is to use libgit/libgit2 if we can
   - showing git dirty status (clean, dirty, not-in-git) in window somehow as first usage of git integration
     (implemented using background thread and /usr/bin/git)

- allow multiple filenames on the command line and just open them all as separate documents/windows

- Add a `^K` command to select the current `{}` scope (using the new bracket matching logic)
    - `^K[` and `^K{` implemented

- `^G` Matching bracket navigation and visual highlighting

- CI fails because "testrun/" does not exist - the custom meson blurb that copies our binary there should just mkdir -p that directory always

- `^KJ` Paragraph format (implemented using clang-format on the current block of text)

- a settings dialog box (and data backend that uses the ~/.turbostar file)
  the first setting would be prefered coding style (which maps to the clang-format predefined types, and has a "~/.clang-format file" as additional option. If a .clang-format file exists in the project that should always win

- a way to call clang-format on the current file
   - needs to save, run the command, then reload, as one nice operation

- `^KR` Insert file

- implement an undo/redo mechanism
    - `^_` Undo
    - `^^` Redo

- search via ^K F could use some autocompletion (similar to file dialog),
     but based on past searches as source for autocomplete. this means we
     need a global list of past search strings, populated both from ^KF and the
     dialog box option 

- run a test coverage analysis to see if whole areas are not covered by the
    test suite

- need to sync the key mapping document in docs/ with recent keyboard
  shortcut additions

- we need to update docs/colorscheme.md based on recent additions to
     main.cpp and the file dialog

- on making the backup ~ file we should use a move/rename style operation
  rather than writing out a new copy; if the disk is full the rename will
  succeed but not the new writeout - the new writeout would thus lead
  to data corruption.

- add ^KL for "go to line" - ask the line number in the status bar and then move the Y cursor to that line

- On saving over an existing file we should make a filename~ style backup file

- File->Save acts as File-Save As in that it asks for a filename - only Save As should ask for a filename
    unless no current filename exists

- we should extend the default test timeout to 60 seconds as we do many delays

- add ^K S as a shortcut for save (not save-as, so use existing filename)

- needs_render = true should become a method so that we can add hooks/etc into
    it centrally later

- src/dialog.cpp uses A_REVERSE
     - needs to get explicit colors instead

- we broke cursor navigation. if the cursor is on the left most character of a line, it does not
    go to the end of the previous line on using the cursor-left key

- our testing framework struggles with finding the turbostar binary as we
  can use different buildroot -- can we teach meson to copy the result to
  our testrun directory?

- code cleanup
    - window.cpp lines 54-106 is a chain of if statements that could
    	be a switch()

- fix search via ^K-F . When done via key bindings (thus status bar), the operation does not actually search. A consecutive ^L does search and to the found item
- file dialog: File Open case. when you navigate (in the file listing section) to some file and hit Enter, you don't go directly to the editor with the file,
    but instead you go first to the entry box at the top of the dialog. THis is a redundant but annoying-to-the-user step, we should just accept enter instantly
- test suite failures need to be fixed; 
   - likely the search item above will fix at least some failures
- show dirty/clean state of the windows in the window title bar somehow, and in the window list menu
- test suite running environment definition
   - currently a bit of a mess, we should make a testrun/ directory and make
     that the directory tests ALWAYS run from as CWD.. This impacts data
     directory paths, where to find the turbostat binary etc etc but at
     least it will be a predictable place.