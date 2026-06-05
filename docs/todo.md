# short term items (fixes needed -- agents can automatically add todo items to this section) -- not in priority order




- since we have github:// and skills://
	- we could add skills by just a git hub url somehow clever so no need for local storage
	- useful for domain activated skills say in the x86 namespace


- Feature: MCP server: if the mcp server is in a directory that has a .git, can we check if there's an update upstream (github?)
      - we could build an auto-update feature!


- feature: github copilot oauth authentication
	- need to read up on this more first how this is supposed to work
	- two parts:
		- part 1: using existing oath key
		- part 2: getting oauth set up

- feature: a "desired_format" optional argument to web_fetch that behind the scenes calls various format converters, example pdf to markdown
	- alternative: a convert_file_format() tool call
	- need to do pro/con between these options

- bug: valgrind does not work in our sandbox

- visual: in the agent interaction, if the result is a markdown table wider than the window, we wrap the table which looks awkward
	- need to just spill to the right instead?
	- or cut down some wide columns to make things fit (replace super long fields with "....")

- sandbox: we should provide the agent a scratch directory space (tmpfs backed) that is explicitly allowed for
  write in the tool security system and sandbox system so that the agent does not need to clobber the actual
  project directory with small python or other scripts it makes to do things

- MCP support									
	- each tool will get a prefix to make sure they are unique
	- each MCP should have its own "uv sandbox"
	- permission model: need to have permission BEFORE executing anything from the project directory
	   (persistent option) but system MCPs are assumed safe
		- concept: system MCPs are on by default, project ones are off by default
	- option to run the MCP in a fully read only sandbox, if the MCP claims to be read only
		- after any UV deps are installed that is
	- asking the MCP what tools it supports should be read only sandbox
	- github integration! (check if there is a newer upstream etc)


# mid term items

- find a security scan tool for javascript/nodejs

- feature: add mouse click interaction on the compaction progress bar to trigger the detailed memory popup dialog (deferred phase)

- feature: style estimator : look at the current codebase and use clang-format with various options to approximate/detect the coding style (detecting/creating a .clang-format from the codebase if none exists), and then send as a summary to the LLM as part of system prompt. See `docs/design-clang-detect.md` for architecture.

- if we have warnings/etc info, the initial system prompt should tell the agent that, or maybe it's an early notification
    - at the end of a compile and there are errors or warnings, we need a system notification to the agent that there is new info
    - low priority

- when a background agent is processing, the cursor flickers badly -- are we redrawing a lot or turning the cursor on/off a lot?
	- not seen recently

- gdbserver notes on how to debug an application nicely
	terminal 1:	gdbserver :1234 ./my_program
	terminal 2:	gdb ./my_program
			target remote localhost:1234

			continue (etc)

- feature: somehow syntax highlighting for specific binary file formats in the hex editor
	- Targets in priority order:
		- PNG images
		- JPEG images?

- a github:// VFS namespace (follow up)
	- implement a persistent disk cache under ~/.cache/turbostar/github_vfs/ for raw files and metadata, with TTL / invalidation checks.

- a few "github" tools
	- create PR
	- fetch PR info

	
	

# long term items
- tool classes
	- x86 asm
	- image edit/convert/select/resize/zoom/rotate
	- PDF ?

- a set of settings (separate dialog!) for a set of tasks, and which model to use for each
	- task 1: summarizing context history
	- task 2: deriving coding style
	- ... more to come over time so we need to make this extensible

- should we use turbo vision?
	- pro: automatic the full look
	- pro: automatic all window/etc interactions working well
	- con: total rewrite and cumbersome framework
	- con: agents struggle with turbo vision framework - it's ancient and not well trained on

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

## 05-06-2026
- fully completed the Tool Family feature: implemented Step 3 including the `activate_tool_family` tool call, prompt rebuilding, and active tool filtering in agent context, with passing unit test coverage.
- implemented a hybrid ELF format inspection solution comprising three new tools belonging to the `"x86"` family: `hex_inspect_range` (semantic range inspector querying syntax highlighters), `elf_list_sections` (ELF section header listing), and `elf_list_symbols` (ELF symbol table search with RE2 pattern filtering).
- created and registered three new unit test suites: `test_hex_inspect_range.cpp`, `test_elf_list_sections.cpp`, and `test_elf_list_symbols.cpp`, sharing mock ELF byte generation via a common `elf_test_helper.h` header, and verified all three tests pass successfully.
- built the first non-base tool family `"x86"`, implementing the `x86_disassemble` and `x86_assemble` built-in tools. `x86_disassemble` integrates Zydis to disassemble machine code in Hex and Base64 formats. `x86_assemble` integrates the system GNU Assembler (`as`) to assemble single instruction strings into space-separated hex bytes. Both tools support 16/32/64-bit CPU modes and Intel/AT&T syntaxes. Added unit test suites and updated documentation. Enhanced `fs_read_binary` to support the new optional `format` parameter (`"hex"` or `"base64"`), allowing it to return space-separated hex bytes.
- implemented Step 1 and Step 2 of the Tool Family feature: defined the `tool_family` structure, mapped all existing tools to the `"base"` family by default via `tool_validator`, mapped MCP tools to their server's family by definition, and added configuration loading/saving/getting/setting support in `config_manager`. Added unit tests in `test_run_config.cpp`.
- implemented thread-per-MCP parallel server startup, and optimized wait loops and exiting signals to prevent shutdown hangs.
- implemented `agent_set_application_binary` agent tool to let the agent configure the main executable for run/debug settings, and added a corresponding unit test.


## 04-06-2026
- fixed a bug where background summaries of episodes and new agent connections did not honor the global model registry default or project settings, falling back to a hardcoded "gpt-4o" model instead of the user's preferred defaults.
- added `/mcp` and `/skills` slash commands in the agent TUI window, enabling users to launch the MCP Servers configuration dialog or list available skills directly from chat.
- clarified the `fs_read_lines` schema description for `end_line` to explicitly state that it is optional and defaults to reading to the end of the file, preventing LLM reasoning loop issues.
- rewrote the MCP configuration and tool dialogs from scratch, removing the problematic group boxes to restore native container-based tab ordering, and spacing controls symmetrically within the 64x20 dialog bounds.
- implemented space key toggling of listbox checkboxes in MCP config and tools dialogs, including selection index preservation on toggle and back-button navigation. Added unit tests in `tests/unit/test_listbox.cpp`.
- implemented Bandit security scanning for Python MCP servers. Discovered Python MCPs have their target script scanned using Bandit (if installed); system-level MCPs are disabled by default if critical high-severity issues are detected, and starting any Python MCP is blocked if Bandit fails the scan. Added a comprehensive test case in `test_mcp_manager.cpp`.
- implemented MCP integration: added "MCP Servers..." menu option under the "Options" top-level menu, hooked it up to event dispatching, implemented TUI configuration and tool dialogs with dynamic state toggling (server process and individual tools) and persistence, and verified implementation with passing E2E and unit test coverage.
- optimized application startup performance by shifting MCP server discovery and initialization to a background thread (`mcp_manager::start_async()`), and added thread-safe locks (`std::mutex` and `std::recursive_mutex`) guarding the tool registry and manager.

## 03-06-2026
- implemented an optional `no_ask` boolean parameter for the `web_fetch` tool. When set to true, `web_fetch` fails silently with a permission error instead of prompting the user, facilitating automated workflows. Updated schemas, validator overrides, and negative/positive unit tests in `test_web_fetch.cpp`, and documented it in `docs/tools.md`.
- implemented a priority-based status message infrastructure (`status_priorities` namespace and `active_status_messages_` map in `editor`) where sources of status text (LSP hover, agent status, diagnostics, and transient warnings/errors) declare a priority. The status bar displays the single highest priority active message, resolving space conflicts and preventing truncation. Added unit tests in `test_vim_emulation.cpp`.
- implemented a pluggable hex editor syntax highlighting interface (`hex_highlighter` and registry) and integrated it with `hex_editor_window`. Added a detailed `elf_hex_highlighter` subclass that parses ELF files (both 32-bit and 64-bit, Little/Big Endian) to highlight ELF headers (Ehdr), Program Header Tables (PHT), Section Header Tables (SHT), and specific mapped sections (like `.text`, `.data`, `.rodata`, and `.symtab`) in distinct foreground colors while leaving the dark blue window background aesthetic intact. Also decodes structure fields under the cursor into detailed descriptions on the status line. Added unit test suite coverage in `test_hex_highlighter.cpp`.
- fixed off-by-one bug in the vertical direction for block-move when the block ends on the last byte of the previous line (whole line), ensuring structural deletion and insertion of whole-line block selections behaves correctly without eating blank lines.
- implemented a custom hex editor window class (`hex_editor_window`) and document class (`binary_document`), integrating auto-detection for binary files using `fs_utils::is_binary_file()`, dynamic wrapping in multiples of 16 bytes depending on window width, a tab-toggled double-column cursor focus (hex tuples and ASCII), overwrite-only typing (auto-growing at EOF), live status bar offset and value formatting (hex, decimal, and ASCII), and robust binary saving with symmetric backups (`~`). Added E2E test coverage in `test_hex_editor.py`.
- implemented a context-aware status bar help message for the `Ctrl-K` block menu. Options are filtered dynamically based on applicability (e.g. selection active, file modified, file open), prioritized (block selection and save actions high, find/navigation medium, compile/other low), greedily packed to fit within `COLS - 2` characters, and re-sorted in a stable defined layout order to preserve muscle memory. Hotkeys are highlighted via `^` caret prefixes. Added a new E2E test suite `test_k_block_help.py` verifying behavior.
- implemented auto-restoration of maximized windows when dragged by the title bar or resized by the bottom-right corner. Resizing starts from the maximized bounds, while dragging restores and centers the window under the mouse cursor. Added comprehensive E2E test coverage in `test_window_maximize.py` verifying Option 1 behavior.
- implemented double-start detection and assertions in `git_manager::start` to prevent thread leaks and std::terminate crashes from duplicate worker loops. Added corresponding unit test coverage in `test_git_manager.cpp`.
- refactored `markdown_utils.cpp` table heuristics `is_table_row` and `is_header_separator` to use lookahead-free RE2 regular expression parsing, and updated `meson.build` dependency definitions for the affected unit test suites.
- refactored `fs_utils.cpp` directory functions to use a common `get_project_dir()` helper, log override events in `set_override_project_dir`, and deduplicate project-specific directories (tmp/history/dumps/dbs) to delegate directly to `get_project_cache_root()`. Added unit tests coverage.
- implemented a smart `fs_utils::is_binary_file` helper detecting both NUL bytes and control character sequences, refactored open-coded checks in `fs_read_lines`, `fs_regexp_lines`, `fs_grep_files`, and `count_lines_in_file` to use the helper, and introduced a unit test suite `unit_fs_utils` verifying text, empty, directory, NUL-based binary, and control-character-based binary files.
- implemented tab-aware visual column vertical navigation (Ghost X tracking) that maps screen coordinates correctly back to line character offsets, preventing visual jumpiness when navigating vertically across lines containing tabs. Added corresponding unit test coverage in `test_document.cpp`.
- optimized `window::draw_content` and `window::draw` to return immediately and bypass redraw operations if selection and bracket-matching highlights are unchanged on cursor-only updates.

- updated the editor main loop and `editor::render` to support and trigger cursor-only rendering (`render(true)`) when a window needs a cursor update but no full screen repaint is pending.
- updated arrow keys (UP, DOWN, LEFT, RIGHT) handling in `window::process_events` to use `invalidate_cursor()` instead of a full `invalidate()`.
- implemented `window::invalidate_cursor()`, `window::needs_cursor()`, and `window::clear_needs_cursor()`, tracking if cursor-only updates are pending.
- updated `window::update_viewport()` to return a `bool` indicating if a scroll occurred, and updated `window::draw` to automatically override/cancel `cursor_only` rendering when the viewport shifts.
- added optional `bool cursor_only = false` argument to `window::draw` and `window::draw_content` across all 5 window subclasses (`agent_status_window`, `agent_window`, `crashdump_window`, `diff_window`, `terminal_window`), preparing the UI layout hierarchy for cursor-only refresh optimizations.
- configured Meson E2E tests to run in offline mode (`UV_OFFLINE=1`), preventing test run DNS/network timeouts.

## 02-06-2026
- made the `type` parameter optional in `fs_replace_lines` tool, defaulting to "replace" when omitted, to make it more agent-friendly. Included comprehensive unit test coverage.
- improved `fs_grep_files` tool to print the target pattern and path while searching, and implemented consecutive duplicate search query detection via a global static tracker to prevent agent infinite search loops.
- optimized milestone boundary classifier loop (`evaluate_dense_layer`) in `context_dnn.cpp` using SSE2 vectorization and parallel register unrolling.
- prevented starting/queuing any background AI summarization tasks during application exit by checking a new thread-safe `is_exiting` flag on `project_manager`, eliminating the 7+ second exit delay.
- isolated `uv run` environments in the Python runner tool by setting `UV_NO_PROJECT=1` and `UV_PROJECT_ENVIRONMENT=.turbostar/uv_env`, preventing any contamination of the user's project configurations or local virtual environments.
- fixed a bug where LLM connection or JSON parsing error messages were stored as episode reactivation hints. The background summary worker now filters out such client errors to keep reactivation hints empty/clean when the LLM server is unavailable.
- implemented "file has changed on disk" detection: checks modification time (mtime) of the active file once every 10 seconds, displays a reload prompt dialog offering to reload or ignore the change, and handles state synchronization to avoid duplicate warnings when changes are ignored. Included comprehensive unit and E2E test coverage.
- implemented native mouse click-and-drag text selection and clipboard copying via OSC 52 escape sequences on mouse release, including visual XOR toggling against persistent block selection, base64 utility consolidation, display-column-to-character-position line offset translation, and comprehensive E2E/unit tests.
- implemented fallback signal handling inside the main executable using `libunwind` to print a clean crash backtrace to stdout when not preloaded with `libturbocatch.so`, resetting the terminal modes on crash.
- implemented a custom `ui_utils::draw_border` helper function to deduplicate double-line and single-line border rendering logic across `dialog.cpp`, `window.cpp`, and `popup_menu.cpp`, avoiding duplicate absolute-coordinate print loops.
- refactored `dialog::handle_event` in `src/ui/dialog.cpp` to remove redundant checking of `action_` and return the result of `ui_container::handle_event` directly.
- implemented a `^K Z` hotkey for "Zap Trailing Whitespace" to trim trailing spaces and tabs from lines (scoped to active selection, or fallback to entire document). Added corresponding unit test coverage in `test_document.cpp` and updated `keybindings.md`.
- refactored `dialog::handle_key` to trigger child controls on ESC via a `press_on_esc()` virtual method on `ui_element` (implemented on `ui_button` and passed via constructor), removing hardcoded string name checks.
- fixed visual bug where button colors were inverted; updated `ui_button.cpp` and `main.cpp` color pairs to match the authentic Turbo Pascal aesthetic (focused text is bright yellow on green, unfocused is black on green with bright yellow hotkey).
- refactored editor prompt and block mode boolean flags (`k_block_mode_`, `q_block_mode_`, `p_block_mode_`, `is_searching_prompt_`, etc.) into a unified `editor::input_mode` enum class.
- updated checkerboard background pattern on empty/unfilled areas to a gray-on-black color scheme (pair 9).
- refactored `editor::set_focus` by extracting the `focus_target` enum-to-string conversion logic into its own global helper function `focus_target_to_string`.
- modified `fs_replace_lines` output format to label the modified range output as `"Code after edit for lines X - Y:"` instead of `"[Modified Section lines X - Y]:"`, making it explicitly clear that this represents the post-edit content.
- renamed `fs_find_in_files` tool to `fs_grep_files` across the codebase (including all class, interaction, test target, documentation, and system prompt references) to explicitly identify it as a built-in replacement for the `grep` shell command.
- added an optional `pattern` parameter to `fs_list_tests` tool supporting case-insensitive RE2 regular expression and substring filtering, and updated the agent system prompt to explicitly recommend using `fs_list_tests` instead of running `meson test --list` / `ctest --show-only` via the shell, preventing agents from falling back to unnecessary shell commands.
- modified `agentcli` to dynamically load the global configuration and resolve the default model from the model inventory (registry) instead of hardcoding `"cli-model"` or `"gpt-4o"`, allowing E2E testing to work out-of-the-box with custom local LLM providers.
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