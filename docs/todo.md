# short term items (fixes needed -- agents can automatically add todo items to this section) -- not in priority order

- add mouse click interaction on the compaction progress bar to trigger the detailed memory popup dialog (deferred phase)

- track Git HEAD hash in software_map.json to detect codebase churn and dynamically adjust scanning aggressiveness

- github copilot oauth authentication
	- need to read up on this more first how this is supposed to work
	- two parts: 
		- part 1: using existing oath key
		- part 2: getting oauth set up
		

- set "uv" working directory to not share; avoid risk of contamination

- a set of settings (separate dialog!) for a set of tasks, and which model to use for each
	- task 1: summarizing context history
	- task 2: deriving coding style
	- ... more to come over time so we need to make this extensible

- a "no_ask" optional argument to web_fetch and maybe some other tools, that causes the tool call not to ask the user for permission but just silently fail

- style estimator : look at the current codebase and use clang-format with various options to approximate/detect the coding style (detecting/creating a .clang-format from the codebase if none exists), and then send as a summary to the LLM as part of system prompt. See `docs/design-clang-detect.md` for architecture.

- do we need a whole wrefresh on a cursor move within the screen? or just update the cursor position
   - a "need_cursor_update" flag would be good in addition to need-screen-refresh,
     that was "small" cursor movements don't need a redraw of the content, only the cursor position and status bar
   - did a change to turn off the cursor blinking while refreshing -- this solve 98% of the problem

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
	- rate limit to once per 10 seconds.
	- dialog offer is "load from disk"

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
	- each tool will get a prefix
	- each MCP should have its own "uv sandbox"
	- permission model: need to have permission BEFORE executing anything from the project directory
	   (persistent option) but system MCPs are assumed safe
	- a menu somewhere so we can turn on and off individual tools
	- option to run the MCP in a fully read only sandbox, if the MCP claims to be read only
		- after any UV deps are installed that is
	- maybe http MCPs are easier? we have some local ones we created ourselves, start with those

# long term items   

- full gdbserver support so we can run the application and single step through it from the GUI
    - we're already 80% there!

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

## 02-06-2026
- updated `git_diff_unstaged` and `git_diff_staged` tool descriptions and the agent system prompt to explicitly recommend using them instead of running `git diff` via the generic `run_shell_command` tool, preventing agents from falling back to unnecessary shell commands.
- updated `fs_find_in_files` tool description and agent system prompt to explicitly recommend using `fs_find_in_files` instead of running `grep` via the generic `run_shell_command` tool, preventing agents from falling back to unnecessary shell commands.
- fixed argument escaping bug in `fs_run_tests` tool for Meson build system where test names containing spaces were split into separate command line arguments, causing test execution failures. All test name arguments are now robustly shell-escaped, and added a unit test.
- fixed a bug where the background episode summarization thread did not use the configured default model from the model list/registry, defaulting instead to the agent's current active model.
- implemented parallel `fs_read_lines` tool call coalescing (merging) within the same assistant turn. When multiple read requests are sent in parallel targeting the same file, they are grouped and merged if the gap between them is within a threshold (20 lines). The parent call reads the contiguous block, and the child calls return a redirection note to avoid redundant file output, saving prompt/context tokens.
- modified `fs_replace_lines` tool output warning logic to omit the shifted by 0 lines warning when the net shift is 0 lines, saving context window tokens.
- implemented OpenAI model auto-import from custom servers: added a Server URL input textbox and an Import button to the model list management TUI dialog, querying the server's /v1/models endpoint, parsing OpenAI-compliant JSON responses, registering all discovered models as free/local entries, showing clean error dialogs on connection/response failure, and including comprehensive unit testing.
- fixed a bug in `fs_replace_lines` tool safety verification where leading and trailing whitespace differences (such as tabs vs. spaces or omitted leading indentation) in `original_text` caused spurious Stage 2 Security Violations. The tool now performs prefix matching on trimmed lines, and added a corresponding unit test.
- integrated auto-saving of dirty documents prior to shell tool executions: `run_shell_command` now automatically calls `save_all_documents()` before spawning command runners, preventing out-of-sync disk reads by external shell commands (like `sed`, `grep`, or compilers) and human-induced workspace drifts.

## 01-06-2026
- implemented Ctrl-W (Delete Word Forward) boundary joining behavior: if the cursor is at the end of the line, Ctrl-W now merges the next line into the current one, and added an E2E test verifying this behavior.
- fixed a Meson build system bug where E2E tests were using stale binaries because they lacked a dependency on the `copy_to_testrun` custom target.
- optimized document window drawing performance by implementing atomic retrieval of line text and attributes, reducing screen-rendering lock/unlock cycles by over 150x.
- fixed Tool Status TUI dialog layout overflow by wrapping the `apt install` package command into multiple lines dynamically and auto-adjusting dialog height.

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
