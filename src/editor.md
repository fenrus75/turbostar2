# Editor Controller (`editor`)

## Goals
The `editor` class serves as the central application orchestrator, state owner, and main rendering loop for Turbostar. Its core responsibilities include:
1. **Application Lifecycle**: Managing NCurses terminal setup, raw mode, color pairs (Turbo Pascal 7 palette), and clean terminal restoration on exit or signal.
2. **Window & Layout Management**: Managing open editor windows, diff viewers, AI agent windows, and interactive terminal windows with support for cascading, tiling, zooming, and cycling.
3. **UI Focus & Event Routing**: Directing keyboard and mouse inputs through a strict focus hierarchy (`focus_target::menu_bar`, `dialog`, `popup`, `window`).
4. **Modal Dialog System**: Hosting Turbo Pascal style modal dialogs (file open, save as, search/replace, compiler settings, prompt user, model selector).
5. **Sandboxed Subprocess Execution**: Spawning and managing interactive background application runs, recording outputs, and supporting CPU performance profiling.
6. **Agent Document Provider**: Implementing `agentlib::document_provider` so AI agents can inspect, open, and modify documents directly in the live editor session.

## Architecture and Constraints
- **Event Domain Splitting**: The event dispatching logic is organized into domain-specific modules:
  - `editor.cpp`: Main frame loop, terminal initialization, window creation and tile/cascade layout.
  - `editor_events.cpp`: Central event router switch statement (`editor::dispatch`).
  - `editor_events_key.cpp`: Keyboard navigation, Wordstar / Joe dialect shortcut maps, macro recording.
  - `editor_events_mouse.cpp`: Mouse clicking, text selection, border dragging, window resizing, scroll wheel.
  - `editor_events_file.cpp`: File opening, saving, reloads, unsaved changes confirmation.
  - `editor_events_build.cpp`: Project build triggering, diagnostic banner rendering, error navigation (F7/F8).
  - `editor_events_lsp.cpp`: Language server diagnostics, hover popups, definition jumping.
  - `editor_events_git.cpp`: Git branch selection dialog, git diff viewer.
  - `editor_events_ui.cpp`: User interaction dialogs, `prompt_user` resolution, inline agent launcher.
  - `editor_events_window.cpp`: Window cycling (F6), zooming (F5), tile/cascade geometry updates.
- **Thread Affinity**: All NCurses rendering and window state modifications occur strictly on the main thread. Background threads dispatch events asynchronously via `event_queue`.
- **Critical Dispatch Invariant**: Any new `event_type` added to `event_queue.h` **MUST** be explicitly handled in `editor::dispatch` inside `src/editor_events.cpp` to prevent silent runtime drops due to the `default: break;` case.

## Lessons Learned
- **Promise Cancellation on Active Dialog Replaced**: In `prompt_user`, if a previous interactive prompt is pending when a second prompt arrives, overwriting the promise without resolving it will cause the first calling thread to deadlock indefinitely. Always resolve orphaned promises with an empty/cancel value before adopting new ones.
- **Latency Spike Diagnostics**: Tracking keystroke latency durations (`latency_spike`) detects un-sandboxed disk I/O or synchronous subprocess invocations that cause noticeable typing stutter.
- **Focus Routing Discipline**: Global shortcuts must be bypassed when modal dialogs or agent input boxes are active to prevent accidental window closures or editor navigation while typing.
- **Dialog Multi-Field Focus vs Prompt Modality**: In multi-field dialogs such as Search & Replace, pressing Enter in the primary query field must advance focus to the replacement field rather than prematurely confirming the dialog with an empty replacement. In interactive prompt-on-replace workflows, matches must be visually highlighted with temporary document selections, and forward search continuation must be enforced to avoid resetting to buffer start when origin is set to entire scope.
- **JOE Keystroke Flow Compatibility**: Wordstar/JOE inline search/replace (`^KF` -> search term -> `Enter` -> `Options (I R B K): R` -> `Enter`) must transition directly into `input_mode::replace_query` (`Replace with: `) on the status bar rather than popping up a modal dialog, maintaining uninterrupted muscle memory. In `replace_prompt`, pressing `A` (All remaining) loops forward from the current match with `cont_search.from_cursor = true` to replace subsequent occurrences without restarting from line 0 and modifying previously skipped matches.
- **Application Execution Path Containment**: In `editor::start_app`, containment checks that restrict binary execution to the project directory workspace are vital when handling agent requests (`!binary.empty()`) to prevent arbitrary execution outside the project. However, user-initiated execution (e.g. via Run/Debug menus where `binary.empty()`) must permit explicitly configured absolute system executables (e.g. `/bin/bash` or custom interpreters) as well as system tools resolved from PATH via `fs_utils::find_executable` (e.g. `python3`) set in the project configuration or test harness.
- **Block Writing vs Document Save As**: `^KW` is reserved for writing the active selected block to disk (and reports "No block selected." if no block is marked). For saving the entire document under a new name ("Save As"), `^QS` and `^QW` trigger the `save_as` dialog, ensuring test harnesses and users have a dedicated hotkey that does not depend on or modify block selection state.
- **Window Display Priority & Null-Safe Sorting**: When sorting windows for Z-index rendering (`editor::sort_windows_by_priority`), `active_win` may be `nullptr` (e.g. at startup or when no window has focus). Comparing `(a == active_win)` without checking `active_win` first evaluates `(nullptr == nullptr)` to true, causing null pointers in window containers to receive maximum priority (`9999`) and fall through to null dereferences on `a->get_last_active_timestamp()`. Always sanitize null entries with `std::erase_if` and qualify active window equality with `(active_win && a == active_win)`.
- **Zero-Allocation Command Trimming**: Using `utf8::trim_view` in `execute_vim_command` eliminates intermediate `std::string` allocations and standard library `erase`/`find_if` passes while robustly supporting both ASCII and Unicode whitespace around commands.
- **Window ID Allocation & Recycling**: Assigning window IDs using `windows_.size() + 1` leads to duplicate IDs whenever windows are closed out of order (e.g. closing window 2 when windows 1, 2, 3 exist reduces size to 2, causing the next window to be assigned ID 3, colliding with window 3). Use `editor::get_new_window_id()`, which scans active window IDs and allocates the lowest positive integer ID not currently in use ($\ge 1$).
- **Hotkey Printed Length with Multibyte UTF-8**: Calculating visible character width of status bar hotkey strings (such as `^B:Beg`) by iterating raw bytes miscalculates length when labels contain multibyte UTF-8 characters (e.g. counting 2 bytes for `é` or 3 bytes for `…`), leading to premature truncation of status bar options. Use `utf8::length(s)` and subtract 1 if the string contains the non-printed hotkey marker `^`.
- **Recursive Agent Collection Across Terminated Parents**: In `editor::collect_agents_recursive`, an early exit when encountering `agent_status::dead` prunes subagent subtree traversal, causing surviving child/grandchild subagents spawned by completed or closed parents to be omitted from `get_all_active_agents()`. The traversal must record visited IDs and continue recursing down `agent->get_subagents()`, filtering out only dead agents from the output collection without terminating the branch exploration.
