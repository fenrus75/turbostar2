# short term items (fixes needed -- agents can automatically add todo items to this section) -- not in priority order

- add mouse click interaction on the compaction progress bar to trigger the detailed memory popup dialog (deferred phase)

- track Git HEAD hash in software_map.json to detect codebase churn and dynamically adjust scanning aggressiveness

- github copilot oauth authentication
	- need to read up on this more first how this is supposed to work

- set "uv" working directory: 

- a set of settings (separate dialog!) for a set of tasks, and which model to use for each
	- task 1: summarizing context history
	- task 2: deriving coding style
	- ... more to come over time so we need to make this extensible

- a "no_ask" optional argument to web_fetch and maybe some other tools, that causes the tool call not to ask the user for permission but just silently fail

- style estimator : look at the current codebase and use clang-format with various options to approximate/detect the coding style (detecting/creating a .clang-format from the codebase if none exists), and then send as a summary to the LLM as part of system prompt. See `docs/design-clang-detect.md` for architecture.

- do we need a whole wrefresh on a cursor move within the screen? or just update the cursor position
   - a "need_cursor_update" flag would be good in addition to need-screen-refresh,
     that was "small" cursor movements don't need a redraw of the content, only the cursor position and status bar

- in the agent interaction, if the result is a markdown table wider than the window, we wrap the table which looks awkward
	- our cut down some wide columns to make things fit

- when a background agent is processing, the cursor flickers badly -- are we redrawing a lot or turning the cursor on/off a lot?
	- not seen recently

- next set of tools for agents:
      - request-access-to-denied file (to add to the security manager, will ask the user)
    - crashdump; 
	- crashdump_read_memory(nr, location, size)
	- crashdump_gdb(nr, command) (over time -- likely to come later)

- sandbox: we should provide the agent a scratch directory space (tmpfs backed) that is explicitly allowed for
  write in the tool security system and sandbox system so that the agent does not need to clobber the actual
  project directory with small python or other scripts it makes to do things

- if we have warnings/etc info, the initial system prompt should tell the agent that, or maybe it's an early notification
    - at the end of a compile and there are errors or warnings, we need a system notification to the agent that there is new info
    - low priority

- detect "file has change on disk" by checking file mtime once in a while as the user is typing

# mid term items

- gdbserver notes on how to debug an application nicely
	terminal 1:	gdbserver :1234 ./my_program
	terminal 2:	gdb ./my_program
			target remote localhost:1234
			continue (etc)


- a github:// VFS namespace (follow up)
	- implement a persistent disk cache under ~/.cache/turbostar/github_vfs/ for raw files and metadata, with TTL / invalidation checks.

- a few "github" tools 
	- create PR
	- fetch PR info

- MCP support
	- each MCP should have its own "uv sandbox"
	- permission model: need to have permission BEFORE executing anything from the project directory
	   (persistent option) but system MCPs are assumed safe
	- option to run the MCP in a fully read only sandbox
		- after any UV deps are installed that is

# long term items   

- full gdbserver support so we can run the application and single step through it from the GUI

- allow for a "companion" screen - basically you log in (ssh) via some other terminal, and connect to a socket provided
  by turbostar and we have a small app (maybe turbostar itself?) connect to that socket and just render that output -
  this gives turbostar a second screen to render to, for example while debugging the main app. Need to figure out
  if this will break ncurses' brain.

- Make skill_manager parsing and discovery fully compliant with the external skills specification (reading metadata, matching URIs, validating schema).

- use a more conformant yaml parser for SKILL.md metadata extraction instead of manual line scanning

- run a small LLM local to decide which model/etc gets to run agent asks
	
- an "auto arrange windows" option of sorts
   - option is all editor files in the right 2/3rd of the screen and the agent status window the right 1/3rd
   - maybe even better, we have a set of templates for certain screen sizes and usages, and use those when appropriate

- a git specific submenu when you click on the branch name in the title bar?
  only useful once we have more than git add implememented, so "long term". We should evaluate this as we add more git capabilities
    - git add
    - ...

# done items (move items here on completion)

## 31-05-2026
- migrated the github:// virtual file system (VFS) provider from cpp-httplib to libcurl easy client interface, eliminating client library crashes and using curl's native environment proxy parsing.
- implemented undo group coalescing (merging) to group consecutive single-character typing, backspacing, and whole-line deletes (Ctrl-Y) into a single logical undo transaction. Coalescing state is cleanly reset upon cursor navigation (arrow keys, word movement, paging), selection changes, or file saving. Added the notify_undo_changed global event to broadcast stack updates to all windows, allowing the live Undo History diff window to update dynamically in real time without lag. Included comprehensive unit tests.

## 30-05-2026
- implemented scheme-based VFS provider abstraction (vfs_provider interface), migrating skills:// and agent:// to a memory VFS provider, and implemented github:// VFS provider supporting raw download caching (LRU, max 50 items), folder API listings, user repo listing, default branch resolution, HTTPS proxies, and optional GITHUB_TOKEN authenticated requests. Updated VFS tools and test suite, verifying all features cleanly.
- implemented bandit security validation for Python execution in `run_python` tool, checking if bandit is installed on the host and running it with `--severity-level=high` filter, returning a security validation failure and blocking execution if any issues are detected. Added unit tests covering both inline code and script file security validation paths.
- implemented interception of glibc __assert_fail() and __assert_perror_fail() in libturbocatch.so to capture failed assertion details (expression/error number, file, line, function) in assertion.txt and parse them in crashdump_manager to prepend a '### Failed Assertion' section at the top of the generated markdown crash report.
- implemented C++ unit tests in `tests/unit/` for all remaining Group 3 agent tools (including git helper tools, subagent management tools, web_fetch, and database/shell tools) to ensure comprehensive test coverage, registering them in `meson.build` and verifying both positive and negative validation/execution flows.
- implemented mouse scrolling support in the AI Agent chat window, dynamically calculating content height limits to restrict scrolling past bounds using `std::clamp`. Added an E2E test verifying both up and down scrolling.
- configured the `unit_document` test to run non-parallel (`is_parallel: false`) in Meson to prevent transient file collision failures during parallel test execution.
- implemented active vs. inactive window border visual hierarchy: borders and widget decorations for the focused window are drawn in bright white/yellow, while unfocused windows are drawn in normal (dimmer) white/yellow.
- implemented linked windows feature to mutually link windows (e.g. Run Output and Debugger (GDB), or AI Agent and Agent Status windows) so that when any window in the group gains focus, all other linked windows are brought to the front of Z-order. Added automated clean unlinking in the `window` destructor to prevent dangling pointers.
- implemented dynamic layout updates in `activate_window()` to adjust the split screen layout (toggling between 70%/30% splits for agent windows and 100% full-screen layout for standard editor documents) dynamically on focus change, while cleanly preserving custom user bounds for non-maximized document windows.
- fixed a nitpick where the "Run in Debugger" menu option was not shaded/disabled when no executable was configured, aligning its behavior with the "Run" menu option.
- added E2E tests `e2e_linked_windows` and `e2e_agent_status_window` to verify focus switching, linked window rendering, layout behavior, and added a focus-switching step to `e2e_window_maximize` to verify that custom shrunk window sizes are preserved on switch.
- implemented window maximize/restore toggling via title bar double-clicks and added Maximize/Restore options directly into the window popup menu (supported both via mouse click and Alt-= shortcut), fully preserving layout restore boundaries and testing it via a new E2E test suite.
- implemented mouse support for window resizing (dragging bottom-right corner) and moving (dragging title bar) with relative drift prevention, coordinate clamping, and ncurses mouse mode upgrade to 1002 (button motion events). Added a comprehensive E2E test verifying behavior.
- fixed mouse scroll event dispatching by routing `mouse_scroll_up` and `mouse_scroll_down` in `editor::dispatch` and targeting the topmost visible window under the mouse coordinate using Z-order sorting. Added an E2E test verifying this behavior and fixed ncurses VT100 scroll region terminal emulation corruption by forcing full redrawing with `redrawwin(stdscr)` on refresh.
- implemented a virtual hook pattern (`on_resize`/`on_move`) in base class `window` bounds changes, overriding them in `diff_window` (to recalculate button positions and diff contents) and `terminal_window` (to resize the emulator and notify running processes/PTY).
- implemented conditional real-time stdout logging for exit delays on shutdown. If no logfile is specified, ncurses is shut down immediately upon editor exit and stdout logging is enabled, resetting the timestamp base and suppressing events under 50ms to prevent stdout spew.
- implemented a variadic `std::format`-style overload for `event_logger::log` that internally wraps `std::vformat` and `std::make_format_args`, simplifying logging calls across the codebase (removed nested `std::format(...)` calls).
- performed a thorough security audit of shell argument validation (`fs_utils::is_shell_safe` and callers) and fixed command injection vulnerabilities across tools by replacing manual single-quoting with robust `fs_utils::escape_shell_arg`.
- implemented a secure-by-design, variadic `std::format`-style API in `command_runner`, `sync_command_runner`, and `fs_utils::execute_command_sync` that automatically shell-escapes all formatted parameters (strings and file paths) while forwarding non-string arguments (like numbers) as-is. Added corresponding comments documenting them as self-escaping to assist code review tools.

## 29-05-2026
- consolidated all UTF-8 helper functions into a single `utf8` namespace (in `utf8.h`/`utf8.cpp`) and refactored the rest of the codebase (including highlighters, document search, and terminal emulation) to reuse them.
- refactored the codebase to use project_manager::get_project_root() consistently as the single source of truth for the project root path.
- implemented "Run in Debugger" (F6 focus toggle, gdbserver socket auto-probing delay, tiled terminal windows, and auto-continue run setting).
- refactored event_logger::get_instance().log calls throughout the codebase to use std::format instead of string concatenation.
- implemented mouse click debugger action buttons ([Break], [Step], [Next], [Cont], [Quit]) drawn on the bottom border of the GDB window.
- implemented unified agent execution and debugging tools (`agent_start_app`, `agent_write_to_run`, `agent_get_run_screenshot`, `agent_terminate_run`) with abstract virtual callbacks inside `document_provider`.
- fixed debugger startup and output hangs by resolving transient PTY read errors and turning off GDB pagination.
- enabled global shortcuts (like `^K Q` to Quit) to work when non-document windows (like the Debugger) are focused.
- resolved CLI tool exit hangs by implementing scope-guarded destructors before calling `std::_Exit(0)` and setting connection/read timeouts on HTTP clients.
- implemented stdin FIFO redirection for debugged processes under GDBServer, allowing user keystrokes in the Run Output window to be sent directly to the application stdin.

## 28-05-28
- implemented candidate executable detection (parsing root meson.build) and project-level configuration options (main_executable, run_arguments, run_target_mode).
- implemented a reusable TUI dropdown input widget (ui_dropdown) with support for typing, scrolling, candidate selection overlays, and mouse interactions.
- added a "Run Settings..." dialog and integrated event handling to enable/disable the "Run" menu item dynamically based on configuration.
- implemented vi/vim command emulation (limited to `<esc>:` for commands `q`, `q!`, `w`, `wq`, `wq!`).

- added tracking of `context_pages_compacted` (active level shifts) and `auto_episodes_forced` statistics to the `/stats` output.
- refactored the background summary worker loop to terminate immediately upon editor close, eliminating shutdown delays caused by LLM summarization calls.
- implemented automatic injection of the archived episodes summary table at startup for new agent sessions to ensure context awareness of paged out history.
- implemented the `agent_list_episodes` tool returning a 2-column markdown table showing episode IDs and reactivation hints.
- implemented visual memory compaction progress bar and token budget display on the horizontal separator line in the AI agent window.
- built the background `compaction_engine` (memory eviction decision engine) using progressive tiered compaction and LRU access sequencing to dynamically page out/evict history down to target token budgets.
- refactored context compaction, boundary tracking, and standardized episode states with `set_episode_state` allowing level 0-2 shifts and level 99 eviction.
- implemented a new `agent_set_timer(seconds)` tool that runs a background timer and injects a `"previously set timer expired"` system message when it fires if the agent is idle.
- implemented optional `async` boolean parameter for `fs_compile_file` and `fs_compile_project` to support background compilation.
- refactored the `editor` constructor to accept an options struct (`editor_options`).

## 27-05-2026
- critical: run_shell_command needs a timeout value as optional argument, default 300
- optional: run_shell_command can use a async optional argument, default False
- --exit-immediately to take an optional "seconds" argument for > 1 second delays

## 26-05-2026
- need to deal with the critical review comments in `review-line.md`
- we should never ever send / commands to the agent
- implement /help that lists all /commands
- implement http proxies in cpp-httplib using http_proxy and https_proxy env variables
- can't type at all in the AI agent window when the agent is thinking -- better to let the user type and queue up the response
- build fail: meson does not check for "dtl" being installed
- cut and pasting into the AI agent window prompt does not work
- critical: In the various git commands, we check branch names etc for correctness but we must accept HEAD~1 and the like, today we reject this due to the ~ character!

## 25-05-2026
- use --background-index on clangd to have a persistent index

## 24-05-2026
- implemented an `append` flag for `fs_write_file`. This provides agents with a secure, 0-turn way to append data to logs, configs, or source files without risking destructive overwrites or needing complex `fs_replace_lines` schemas. The tool automatically injects a missing newline (`\n`) if the target file does not end with one to prevent concatenating lines.
- implemented a comprehensive model configuration system. Users can now view, add, edit, and delete AI models via a new "Models..." dialog under the Options menu. Model configurations (including URLs, API keys, and costs) are persistently stored in `~/.cache/turbostar/models.json`. The system supports setting a default model, which is saved to the main `~/.turbostar` configuration file.
- implemented `fs_list_tests` tool and updated `fs_run_tests` to support selective test execution. `project_manager` now parses `meson test --list` on demand to provide a list of known tests. `fs_run_tests` accepts an optional `test_names` array to run only specific tests. Both tools intelligently resolve the build directory relative to the repository root for robustness across different working directories.
- implemented an interactive "Diff View" for document undo history (accessible via `^Q H`). It allows users to visualize changes between any two points in the undo stack using a unified diff format. The view supports "time travel" navigation with `[<< Prev]` and `[Next >>]` buttons (or arrow keys) and a `[Restore State]` option (or `Enter`) to roll the document back to the selected historical state. The diff context dynamically scales to fill available vertical space.
- dynamically resize the "Save Changes" dialog to accommodate long filenames (up to the screen width). If the filename is still too long, a new `fs_utils::shorten_filename` helper intelligently truncates it by preserving the basename and root directories while replacing the middle with `....`.
- implemented a more powerful `fs_find_in_files` tool for codebase-wide regex searches. It uses high-performance `mmap` zero-copy scanning for disk files and checks live, dirty editor buffers. Results are formatted as clean markdown, with a configurable cap (default 50) that falls back to listing filenames to protect context limits.
- fixed `^K F` (Find) prompt usability. Pressing `<ESC>` or `Ctrl-C` while typing a search query or search options now immediately aborts the search and clears the prompt from the status bar.
- implemented ANSI escape code stripping in `interaction_terminal`. Terminals often output color codes or cursor movements (like `ls --color`) that garble the TUI. A fast scanner now "eats" standard ANSI CSI sequences (`\x1b[...]`) and replaces unknown/incomplete escapes with spaces, preventing UI corruption and potential security spoofing.
- lowered the CPU priority of background language servers (`clangd`, `pylsp`) by launching them via `nice -n 10`. This prevents heavy indexing tasks from stuttering the editor UI.
- implemented a fuzzy-search offset hint for `fs_replace_lines`. If the agent provides the wrong line number but the exact target string exists within +/- 10 lines, the tool now returns a helpful error message pointing the agent to the correct line, preventing it from getting "stuck" or needing to re-read the file.
- implemented `/quit` slash command in the agent window. Typing `/quit` in the multiline edit box now immediately closes the agent window, providing a fast keyboard-driven workflow.
- simplified `fs_read_lines` UI reporting: the tool now detects when a whole file is being read and displays "Read whole file (X lines)" instead of confusing line ranges (e.g., 1-10000 of 231).
- implemented `pop_todo()` tool that returns and removes the first item from the agent's todo list, enabling sequential task management.

# Done (long term)

- need to consider running the e2e tests via "uv" as they use a non-standard pip library
	- this causes all tests to fail in github, but also in our sandbox
- a cost goal per model "free" (e.g. local inference), vs "paid" (cost per token) so we can adjust our context compaction
    algorithms to optimize with the goal in mind

- fs_find_in_files -- allow an option to get a few lines of context in the return
     - need to decide on error format
- we have a delay at exit -- annoying to the user
- we need to have project level .turbostar files so we can have per project overrides.
 	-	the settings dialog will need to grow a "save as global defaults" button - default save should be per project
	- we load the global first then per project, so that it overlays

- per project turbostar setting file
	- coding style, build system and the like are really per project -
	  the global settings are just a default for per project

- an --agent "string" command line option that 1) starts in the agent window and 2) sends the "string" as first user message

- a "--model <name>" command line option to pick a specific model as default for sessions

- take the linux-kernel .clang-format, build it into our binary and add a linux-kernel style to the preference dialog for clang-format
	https://raw.githubusercontent.com/torvalds/linux/refs/heads/master/.clang-format
	- we should write it out if this style is selected, if there is no clang-format yet

- undo segments need an optional name, and we can ask the AI for a description
    - search/replace is another automatic name

- the live agent status window does not seem to update with tokens used anymore
  -- verified via /stats that the backend atomic counters (tokens_tx, etc.) are remaining at 0. This means `api_formatter.cpp` is failing to parse the `usage` block from OpenAI/Gemini streams.
  -- context usage is also shown as 0 which should come from our own data!

- format markdown block (` `  or ``` ... ```) in a different color/style in the
  agent output screen (or bold or ..)

- git_add tool and git_commit tool for files that are in the editor but not saved -- we need to decide   
	  if we want to auto-save or ask the user -- the agents edits are in the editor, not on disk
          likewise for compile etc tasks
- - enter_plan_mode, exit_plan_mode
- audit meson.build to see if our testcases should link to all the .cpp or .a files they do -- we relink too many files when we touch a single agent file (replaced libtools.a linkage with compiling only the required entry/security source files directly for each test)
- we should make an option under help menu for "Tool status" that lists if all required dependencies are installed (gdb, gdbserver, clangd, clang-format, python3-bandit), displaying [X] for yes and [ ] for no with a cut-and-pasteable install command for any missing dependencies