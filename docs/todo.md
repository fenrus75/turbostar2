# Web page todos (in `docs/index.html`, `docs/ai.html` and `docs/editor.html`)

## Webpage Screenshots to Take
- [ ] Screenshot of the website homepage running in a browser showing the retro-modern Turbo Pascal HSL color scheme
- [ ] Screenshot of the compile context optimizer output in the terminal showing raw build logs vs. minimized diagnostics
- [ ] Screenshot of an agent interactive debugging session with parallel GDB execution and stdin interactions
- [ ] Screenshot of an inline agent running automated security scans via `bandit` on a C++/Python file

## Webpage todo items (do not delete this header line)



# short term fixes -- not in priority order, agents can add and remove items as they come up (do not delete this header line)

- feature: our security review tool/agent should try to populate the proposed_fix field

- feature: parse meson.build to find the application name for "run" if none has been configured

- fs_replace_content improvement: tabs vs spaces seems to confuse the agent

- feature: a markdown_to_html filter 
	- should be straightforward structural conversion
	- almost a line by line regexp, after some boilerplate headers / footers


- nit: the hexeditor has a size limit that's a bit on the small side -- maybe we should check system memory size and on large systems
    increase the limits?

- feature: subagent creation wizzard dialog

- feature: model aliases -- have a set of aliases we can map to actual models, and we can have our "Task Models" UI to select models 
   for specific tasks set up these aliases.
	- base
	- plan
	- review
	- code
	- image



- feature: in LLM response, instead of doing **Bold** like that, actual show it as bold?
	- need to be careful with text wrapping and line lengths or the dialog box will draw incorrectly

- feature: add a /rescan TUI slash command/shortcut to hot-reload custom subagents inside subagent_manager during runtime.

- feature: become an A2A server for our agents

- feature: become a client to A2A
	we need to parse agent cards and hook them up to our subagent directory

- feature: build an A2A directory
	- maybe a .local style directory service? is there a protocol for autodiscovery?

- feature: have a turboserver mode where we are headless but only serve A2A requests?

- feature:a /command action that activates a tool family (via menu?)

- essential feature: a /command action for /clear of agent history -- basically a new connection

- feature: separate model name database of famous models for default properties
  should investigate the compile time json-to-struct stuff
	- https://raw.githubusercontent.com/BerriAI/litellm/refs/heads/litellm_internal_staging/model_prices_and_context_window.json

- feature: separate model provider (server) database for easy population of model servers
	- need a smart combo box or radio box for "from database" vs "custom" so that it is easy to add either

- feature: have a separate model option for "plan mode" phase

- feature: similar to "run with gdb" we should have a "run with perf record" so that the agent can natively do performance analysis/get perf data
	- need to check the sysfs to see if this is available, if not just hide it entirely
	- perf stat summary?
	- hot functions
	- fs_read_lines option to get per line perf data?
	- not just the agent tool -- also in the editor!

- feature: code review enhancements
	- we can provide upfront a set of static analysis data to the code review agent
		- known compile / LCP warnings
		- cyclic complexity scores?
		- cpplint or other similar tools
		- -fanalyzer output ? should maybe add to fs_compile_file when in code review agent
		- for python we have our security analyzer -- we should run this when reviewing python files
		- if we had backtraces/crashes that involve this file we should include them in context or at least reference them with the toolcall to get the details

- feature: we should create a code review agent file in agents/
	- the code review agent should have access to the security plugin tool namespace

- bug: the "view review items" overview box is still terrible due to lack of working word wrap on long lines -- we may need to just cut these off instead?

- meta feature: helper agents
	- well rounded subagents that have custom tools available to it for specific higher level tasks, can be used by the main agent as if they are fancy tool calls
		- we started this with code review kind of
	- should be able to suggest model names/capabilities to be run in -- say a vision model for image processing tasks
	- should be able to register /slash commands
	- skills that plug into specific subagent types only
	- should have prefered color set + logo for the grid view


	- image processing
		- [ ] file format conversion
		- [x] cut out parts of image into new image (crop)
		- [x] rescale image / rotate / ..
		- [x] to grayscale conversion
		- [ ] basic filters? gimplib?
		- [ ] USB camera access
		- [ ] opencv segmentation/yolo/etc done local


	- file format conversion
		- PDF to XYZ
			- with persistent cache, quick conversion followed by background slower-but-more-accurate conversion
		- OCR

	- english grammer/spelling/clarity

	- markdown toolbox
		- [x] to_markdown filter 
		- [ ] markdown manipulators

	- performance
		- linux perf integration
		- intel performance skill integration

	- homeassistent / home automation

	- code review

	- security review
		- plug in static local scanners
		- maybe fuzzing helpers?

	- fpga /verilog agent?



- feature: better integration for code coverage in the main editor
	- color coding in the left window decoration?
	- or color in the editor window with a view mode with a toggle key

- bug: The agent window text renderer silently truncates large blocks of concatenated system messages. Specifically, when multiple system messages merge into the same visual turn (e.g., initial system prompt + `/save` outputs or `/help` outputs), `wrap_text` or `markdown_utils::align_all_tables` deletes the text between the top few lines and the bottom few lines. This caused the E2E mouse scrolling test to fail because the chat history was artificially shortened.

- feature: since we have github:// and skills://
	- we could add skills by just a git hub url somehow clever so no need for local storage
	- useful for domain activated skills say in the x86 namespace

- Feature: MCP server: if the mcp server is in a directory that has a .git, can we check if there's an update upstream (github?)
      - we could build an auto-update feature!


- bug: SSH forwarded X11 sessions do not work well in our sandbox

- bug: valgrind does not work in our sandbox


- feature: sandbox: we should provide the agent a scratch directory space (tmpfs backed) that is explicitly allowed for
  write in the tool security system and sandbox system so that the agent does not need to clobber the actual
  project directory with small python or other scripts it makes to do things

- feature: MCP support enhancements
	- each tool will get a prefix to make sure they are unique
	- each MCP should have its own "uv sandbox"
	- permission model: need to have permission BEFORE executing anything from the project directory
	   (persistent option) but system MCPs are assumed safe
		- concept: system MCPs are on by default, project ones are off by default
	- option to run the MCP in a fully read only sandbox, if the MCP claims to be read only
		- after any UV deps are installed that is
	- asking the MCP what tools it supports should be read only sandbox
	- github integration! (check if there is a newer upstream etc)

- Feature: Implement Input Coalescing and Refresh Throttling in the main editor loop (described in docs/refresh-throttling-proposal.md) to reduce terminal redraw overhead and input latency under fast repeat keys or paste streams.


# mid term items

- wayland virtual server so that gui apps can be debugged cleverly 

- feature: make an agent file for https://www.docling.ai for pdf to markdown conversion  -- docling is HUGE! not for the faint of heart
	- postpone this


- find a security scan tool for javascript/nodejs -- semgrep is a start

- feature: add mouse click interaction on the compaction progress bar to trigger the detailed memory popup dialog (deferred phase)

- feature: style estimator : look at the current codebase and use clang-format with various options to approximate/detect the coding style (detecting/creating a .clang-format from the codebase if none exists), and then send as a summary to the LLM as part of system prompt. See `docs/design-clang-detect.md` for architecture.

- if we have warnings/etc info, the initial system prompt should tell the agent that, or maybe it's an early notification
    - at the end of a compile and there are errors or warnings, we need a system notification to the agent that there is new info
    - low priority

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
- feature: somehow do python plugins for agents and tool calls?

- full gdbserver support so we can run the application and single step through it from the GUI
    - we're already 80% there!

- allow for a "companion" screen - basically you log in (ssh) via some other terminal, and connect to a socket provided
  by turbostar and we have a small app (maybe turbostar itself?) connect to that socket and just render that output -
  this gives turbostar a second screen to render to, for example while debugging the main app. Need to figure out
  if this will break ncurses' brain.

- Make skill_manager parsing and discovery fully compliant with the external skills specification (reading metadata, matching URIs, validating schema).
	- need to check what is missing now that we have a real YAML parser

- run a small LLM local to decide which model/etc gets to run agent asks

- an "auto arrange windows" option of sorts
   - option is all editor files in the right 2/3rd of the screen and the agent status window the right 1/3rd
   - maybe even better, we have a set of templates for certain screen sizes and usages, and use those when appropriate


- a git specific submenu when you click on the branch name in the title bar?
  only useful once we have more than git add implememented, so "long term". We should evaluate this as we add more git capabilities
    - git add
    - ...

- migrate role-based tool permission checks (in `confirm_code_review_item`, `resolve_code_review_item`, and `perform_code_review`) to silent tool families (prefixed with `:`) to decouple the tool registry from the C++ `agent_role` enum and allow dynamic plugin permissions.


# done items (move items here on completion)

## 16-07-2026
- Code review severity filter enhancement: Upgraded the severity filter in `codereview_manager::list_code_review_items` to treat the target level as a lower bound threshold rather than an exact match (e.g. querying `"medium"` now returns medium, high, and critical issues). Clarified the schema parameter description for the `"severity"` argument in `list_code_review_items.h` to explicitly guide the agent on the accepted values and the "or more severe" filtering behavior. Added test coverage in both `test_codereview_manager.cpp` and `test_list_code_review_items.cpp` to verify correct filtering and prevent regressions.
- Git Blame porcelain parsing fix: Fixed a bug in `git_blame_entry.cpp` where the tool was extracting the line number in the original commit (the first integer after the hash in `--porcelain` output) instead of the line number in the final/resulting file (the second integer). This discrepancy caused duplicate, missing, and incorrectly mapped lines in the final markdown table whenever line edits shifted line offsets. Added a new unit test case to `test_git_blame.cpp` that commits shifted lines and validates correct line blame attribution.
- Crash address logging in turbocatch: Implemented an async-signal-safe pointer-to-hex string formatter `safe_ptoa()` in `crash_catcher.c` and updated `write_info()` to capture the faulting memory address from `info->si_addr` when a signal is caught. Querying `REG_ERR` from the CPU registers in `ucontext_t` when a `SIGSEGV` or `SIGBUS` occurs allows the catcher to identify and log whether the crash was a read or write operation (by appending `(read)` or `(write)` to the address). Added an `is_addr_valid` parameter check so that the `CrashAddress` line is only written for signals where memory address information is defined and valid (`SIGSEGV`, `SIGBUS`, `SIGFPE`, `SIGILL`), suppressing misleading stack garbage or default `0x0` values for assertion/abort signals (such as `SIGABRT`). It also captures `info->si_code` to explicitly output the crash type (`Type: SEGV_MAPERR` or `Type: SEGV_ACCERR`). Added unit test assertions to `test_assert_fail.cpp` verifying both the write access type and `SEGV_MAPERR` type.
- Unified ANSI stripping: Replaced the static `strip_ansi` implementation inside `filter_registry.cpp` with the shared `utf8::sanitize_terminal_output` function. Upgraded the central stripper to detect and clean both raw ASCII 27 escape characters and literal string representations (such as `\x1b`/`\x1B` or `\u001b`/`\u001B` sequences) commonly output by LLMs and terminal logs. Added unit test assertions to verify correct stripping of literal escape strings.
- Troff code block formatting suppression: Modified `troff2md` to inspect the existing `fill_depth` counter (which tracks active code blocks/no-fill regions) and suppress markdown bold (`**`) and italic (`*`) markup generation when `fill_depth > 0`. Fixed `test_tools.cpp` to run against the real `troff2md` translation filter instead of a hardcoded mock, and added a test case verifying code block suppression behavior.
- Debugger terminal output sanitization: Relocated `sanitize_terminal_output` to a shared helper in the `utf8` namespace. Added `sanitize_recorded_data_` property and setter to `terminal_window` and enabled it automatically for GDB debugger split windows. This ensures GDB recording output is sanitized and simplified (ANSI escape codes stripped) while application stdout output is recorded verbatim.
- Write to run output recording: Added an optional `output` boolean parameter to `agent_write_to_run`. When set to true, it starts recording raw stream output written to the terminal emulator, resets the modification timestamp immediately on write to prevent premature pre-settled status, waits for terminal output to settle, and returns the accumulated captured terminal writes directly in the tool response.
- Screenshot settle optimization: Added an optional `settle` boolean parameter to the `agent_get_run_screenshot` tool. When enabled, the tool queries `get_run_last_modified_age()` to wait up to 3 seconds for the terminal's screen activity to stabilize (requiring no screen updates for at least 250 ms) before capturing, ensuring highly stable and complete visual snapshots.
- Terminal emulator modification tracking: Added std::chrono-based last modified timestamp tracking to `ansi_terminal_emulator` (`src/ui/ansi_terminal_emulator.cpp`/`h`). Declared a new public helper `get_milliseconds_since_last_modification()` to allow caller checking of terminal activity/settle times.
- Image composition tool: Added the `image_compose(main_image, small_image, x, y, [output])` tool to the `image_basic` dynamic plugin. This tool overlays/composes the small image onto the main image at the specified pixel coordinates using GraphicsMagick composite operations, allowing agents to dynamically construct compound layouts or overlays. Added `unit_image_tools` test coverage.
- GDB automatic pending breakpoints: Appended `-ex "set breakpoint pending on"` to the GDB startup command in `editor_events_ui.cpp`. This configures GDB to automatically make breakpoints pending on future shared library loads instead of prompting the user, preventing LLM agents from getting stuck on interactive confirmation questions.
- Tool families website documentation: Added a new split-screen detail section and summary card to `docs/ai.html` explaining how the Tool Families feature manages context/prompt bloat and helps agents dynamically activate workspace capability groups (like `git` or `image`) on demand.
- Git tool family unification: Created a dedicated `"git"` tool family for the 16 `git_*` version control tools. This prevents git schemas from bloating the prompt context of specialized subagents (like read-only searchers, planners, or security auditors) that do not require version control. Pre-registered the `"git"` family in `tool_registry`, configured the parent/developer agent and the `self` subagent clone to activate `"git"` by default, and updated related unit tests.
- Image tools relocation: Relocated `image_import` and `image_export` tools from the core `libtools` target to the dynamic `image_basic` plugin module. This isolates the heavy GraphicsMagick dependency entirely within the dynamic plugin, preventing the core application from linking against it. Converted their static registrations to explicit, lifecycle-managed functions.
- Image export format conversion: Updated `image_export` tool to load and save the image via GraphicsMagick when exporting. This ensures that the file format is automatically converted on-the-fly to match the file extension of the target filename (e.g. converting a JPEG VFS image to a PNG file). Included a try-catch fallback to raw file copy to handle non-image files gracefully (e.g. dummy test files). Added a unit test in `test_image_tools.cpp`.
- Hexedit tool parameter unification: Renamed the parameter `offset` in the `hexwrite` tool schema and implementation to `start_offset`, unifying it with `hexdump` and `hexinspect`. Updated all related source code files and unit tests.
- Image tool family description & schema enhancements: Resolved all issues from `image-review.md`. Unified URI scheme to `images://` (plural), explained `by-sha256` content-addressed subpaths, clearly defined "alias" vs "VFS URI" in all tool descriptions, clarified that `filename` is relative to project root in `image_import` and `image_export`, and added a detailed chained example to the image family registration metadata.
- Agent window thumbnail rendering fix: Updated `agent_window`'s `__THUMBNAIL__:` handling to parse and cache the new cell-based quadrant JSON format instead of the old raw pixel arrays. Integrated wide-character ncurses rendering to display these quadrant cells cleanly in the agent interaction logs.
- Tool family capability guidance: Added support for registering an optional guidance string for tool families in the `tool_registry`. When an LLM activates a tool family using `activate_tool_family`, this guidance string is returned to teach the LLM about the capabilities and usage guidelines (e.g. explaining the `image://` virtual scratch namespace for the `image` tool family). Added unit test coverage in `test_activate_tool_family.cpp`.

## 15-07-2026
- Image VFS Manager dialog sizing fix: Compacted the vertical flows inside the Image VFS Manager dialog by setting their spacers to 0, reducing the dialog's height from 24 to 15. This prevents the dialog from exceeding the terminal height (24 lines) and having its top title bar containing "Image VFS Manager" clipped off-screen. Also converted thumbnail character rendering to use wide ncurses APIs (cchar_t and mvadd_wch) to prevent screen alignment corruption. Resolved the e2e_image_manager E2E test failure.
- TUI dialog button hotkeys: Relaxed the hotkey matching logic in ui_button to allow plain/positive character codes to trigger buttons globally within dialogs. Safe isolation with text input fields is maintained by ensuring that focused text editors get the first opportunity to consume keys. Added test_button_hotkeys unit test covering focused vs non-focused scenarios.
- JSON syntax highlighter: Implemented a custom json_highlighter that parses syntactic delimiters (braces, brackets, colons, commas), key strings vs value strings, booleans, null, numbers, and single line comments, applying distinct retro ncurses colors. Added unit_json_highlighter test verifying key/value/number/punctuation highlighting.
- color dialog dangling pointer fix: Resolved a segmentation fault in the Syntax Highlight Colors dialog triggered when selecting custom colors. Fixed the dangling stack reference in the color picker's callback by capturing the listbox pointer via a value-captured std::shared_ptr holder. Added a dedicated unit test `test_syntax_colors_dialog` verifying color selection without crashes.
- generalized code block syntax highlighting: Generalized chat code block syntax highlighting to support C/C++, Python, HTML, Markdown, and Verilog. Replaced the hardcoded extension mapping with dynamic `supports_language` queries against `highlighter_registry`. Extended `test_agent_highlight` to verify Python keyword/comment highlighting.

## 14-07-2026
- plan mode persistence: Fixed the plan mode persistence bug across editor exit/restart by serializing is_planning, planning_start_index, and plan_file variables in save_conversation and restoring them in load_active_state. Added unit test coverage to test_activate_tool_family.cpp.
- double and triple click copy: Implemented software double-click word selection and triple-click paragraph selection (consecutive non-blank lines). Automatically copies selected text to the clipboard via OSC 52 sequences, similar to shell terminals. Added tests/e2e/test_double_click_copy.py covering both actions.
- plugins dialog spacing: Fixed the Help->Plugins dialog's spacing issues by adding an optional spacer parameter to create_message_dialog, allowing compact single-spaced paragraph rendering. Cleaned up trailing and nested empty line labels in editor_events_ui.cpp.