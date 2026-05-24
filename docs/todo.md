# short term items (fixes needed -- agents can automatically add todo items to this section) in random order

- usability: in the agent edit box, implement /quit command as a way to close the agent window
  (we will need to add other common slash-commands over time so lets make this somewhat flexible)

- a more powerful grep_search

- the save changes dialog is sometimes to small for larger filenames. We need to dynamically grow it
  (very ugly screen corruption currently)

- fs_run_tests: allow a list of strings of test names (optional) -- default is run all

- fs_list_tests: give back a markdown table of valid test names (may only be possible after the first test run?)

- allow <ESC> and Ctrl-C to exit/abort a ^KF search
  (usability)

- we need to tackle compaction at some point

- investigate using PCH's in meson to speed up compiling of things that use
  our json libraries

- an model configuration file, and a menu/dialog box to add more, and activate/deactivate models

- git_add tool and git_commit tool for files that are in the editor but not saved -- we need to decide   
	  if we want to auto-save or ask the user -- the agents edits are in the editor, not on disk

- take the linux-kernel .clang-format, build it into our binary and add a linux-kernel style to the preference dialog for clang-format

- support for API keys for models -- need some basic security so that the keys don't leak out
   - including masking this config file in our sandbox

- style estimator : look at the current codebase and use clang-format with various options to approximate/detect the coding style (detecting/creating a .clang-format from the codebase if none exists), and then send as a summary to the LLM as part of system prompt. See `docs/design-clang-detect.md` for architecture.

- automatic software map : markdown tables with key classes and functions, and where they are defined and implemented
	- need to parse class hierarchy - base classes over derived
	- this is an extension to the existing "Key files' section which is currently only path based


- a "run" option to run the application from the menu, where we temporarily
  exit ncurses (but catch crashes etc)

- do we need a whole wrefresh on a cursor move within the screen? or just update the cursor position
   - a "need_cursor_update" flag would be good in addition to need-screen-refresh,
     that was "small" cursor movements don't need a redraw of the content, only the cursor position and status bar

- exit is not always instant when using the agent -- it seems we wait for some agent interaction to finish?
   -- we either need to abort, or figure out how to get ourselves to a background state (which is tricky with threading)

- in the agent interaction, if the result is a markdown table wider than the window, we wrap the table which looks awkward
	- our cut down some wide columns to make things fit

- when a background agent is processing, the cursor flickers badly -- are we redrawing a lot or turning the cursor on/off a lot?

- implement a generic "diff view" that can visualize the changes in the last undo group.
  This is essential for reviewing automated edits from inline agent operations.

- next set of tools for agents
    - next set of tools for agents
      - request-access-to-denied file (to add to the security manager, will ask the user)
        - a set of LSP tools to help code navigation
    	- gdbserver -- allow interactive debug of an app (especially a crash) by the LLM
	        - read memory, get registers
    - crashdump; 
	- crashdump_read_memory(nr, location, size)
	- crashdump_gdb(nr, command) (over time -- likely to come later)
    - enter_plan_mode, exit_plan_mode

- sandbox: we should provide the agent a scratch directory space (tmpfs backed) that is explicitly allowed for
  write in the tool security system and sandbox system so that the agent does not need to clobber the actual
  project directory with small python or other scripts it makes to do things

- syntax highlighting of trailing whitespace is annoying if you're still typing the line.. any space you type
   instantly turns red. Need to maybe know which line the cursor is on or something, or wait for 10 seconds or .. or ..
	maybe we need to delay any syntax coloring/checking update until no typing happened for a couple of seconds or hit enter/change Y cursor line
   - not urgent and needs more thought



- if we have warnings/etc info, the initial system prompt should tell the agent that, or maybe it's an early notification
    - at the end of a compile and there are errors or warnings, we need a system notification to the agent that there is new info




# mid term items


- support `git_push` with `force=true` by utilizing the `ask_user` tool to require explicit human authorization before execution.


- mouse support for resizing windows (bottom right corner) and moving (title bar)

- MCP support

- send per repository instructions to the LLM at startup similar to GEMINI.md -- or we use exactly GEMINI.md

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

## 24-05-2026
- implemented ANSI escape code stripping in `interaction_terminal`. Terminals often output color codes or cursor movements (like `ls --color`) that garble the TUI. A fast scanner now "eats" standard ANSI CSI sequences (`\x1b[...]`) and replaces unknown/incomplete escapes with spaces, preventing UI corruption and potential security spoofing.
- lowered the CPU priority of background language servers (`clangd`, `pylsp`) by launching them via `nice -n 10`. This prevents heavy indexing tasks from stuttering the editor UI.
- implemented a fuzzy-search offset hint for `fs_replace_lines`. If the agent provides the wrong line number but the exact target string exists within +/- 10 lines, the tool now returns a helpful error message pointing the agent to the correct line, preventing it from getting "stuck" or needing to re-read the file.
- implemented `/quit` slash command in the agent window. Typing `/quit` in the multiline edit box now immediately closes the agent window, providing a fast keyboard-driven workflow.
- simplified `fs_read_lines` UI reporting: the tool now detects when a whole file is being read and displays "Read whole file (X lines)" instead of confusing line ranges (e.g., 1-10000 of 231).
- implemented `pop_todo()` tool that returns and removes the first item from the agent's todo list, enabling sequential task management.
- implemented a "project layout" feature that provides the agent with a markdown overview of the project's directory structure in the system prompt. This uses a background thread to inventory the project, counting source files, headers, and documentation/config files, selecting the most relevant directories (top 15-18) based on complexity and density.
- implemented `crashdump_clear` tool to allow the agent to remove stale crash reports after investigation.
- implemented automatic `.clang-format` parsing and minification. A minified version (comments/empty lines removed, capped at 100 lines) is now appended to the system prompt of both the main agent and inline surgical agents, ensuring generated code aligns with project styling.
- implemented persistent cursor positions across restarts with a 25-file LRU policy. File paths are now stored as absolute paths in `.turbostar_history` using a robust `x y path` format that handles spaces in filenames.
- fixed a typo by renaming `AGENT.md` to `AGENTS.md` throughout the project management and documentation logic.
- fixed an unused parameter warning in the `agent_window` constructor by removing the redundant `global_queue` parameter and updating all call sites.
- fixed the `clangd` OOM issue by refactoring LSP management.
 The `lsp_manager` is now a component of the `project_manager` class, ensuring a single LSP instance per workspace. Access to LSP features is now mediated via delegation methods in `project_manager`, hiding implementation details and preventing redundant server launches.
- implemented `git_push` with `force=true` support. When the agent attempts a force push, it requires explicit "Allow" or "Deny" confirmation from the user via the `prompt_user` dialog (permissions are not remembered).
- we need an `fs_utils` helper to locate `libturbocatch.so` in a few key places (e.g., build directory, installation directory, etc) for reliable `LD_PRELOAD` injection.
- fixed the `ui_radio_choice` drawing logic so that the cyan background color extends fully behind the padded spaces to match the alignment box width.
- fixed an LLM race condition by removing automatic `start_processing` upon `inject_context` for system prompts, ensuring the agent doesn't prematurely trigger without a user prompt.
- implemented `run_shell_command` tool. Agents can execute arbitrary bash commands via an in-memory session permission system. A default system prompt strongly urges the use of built-in tools over shell commands to prevent nagging the user with security prompts. Commands containing ANSI escape characters are blocked.
- implemented `web_fetch` tool. The agent can now fetch content from URLs using `curl`. Implemented a robust domain-based permission system (`allowed_domains.txt`) asking users for Once/Always/Deny/Deny Always approvals (blocking Always for local IPs).
- the whole crashdump approach needs a rethink, the world is 100x more complex than assumed
- "Spell check document" option in the Agent window that just runs a prompt and updates the document error list
	- this would be a great addition to our ^P menu, to spell check (and fix) text (for non code) or comments and user strings (for code)
- disabled saving operations ("Save", "Save As", "Save All") for read-only buffers (like the Agent window), graying out the menu items and ignoring the respective keyboard shortcuts (`^KS`, `^KW`, `F2`).
- passed the build directory (via `config_manager::get_instance().get_build_directory()`) to the `clangd` LSP using the `--compile-commands-dir` flag so it correctly locates `compile_commands.json`.
- implemented `git_push` tool. Force pushing is currently explicitly rejected in the schema validator.
- implemented Phase 3 Git tools: `git_checkout_branch`, `git_diff_from_branch`, and `git_pull`, ensuring all branch arguments strictly pass `fs_utils::is_shell_safe`.
- implemented `git_unstage` using `git restore --staged -- <paths>` with a fallback to `git reset HEAD`, handling new/empty repositories properly.
- implemented `git_init` tool with security checks to prevent running if a `.git` directory already exists.
- implemented Phase 2 mutating Git tools: `git_add` (with array path validation), `git_restore`, and `git_commit` (which writes the message to a secure tempfile to avoid shell injection entirely).
- implemented Phase 1 read-only Git tools: `git_diff_unstaged`, `git_diff_staged`, and `git_log`, providing safe access to sanitized raw patch output.
- created `fs_utils::is_shell_safe` to strictly filter parameters bound for shell execution using an explicit character allowlist.
- implemented `fs_mkdir` tool for the agent to recursively create directories like `mkdir -p`.
- if we have a dirty file, and try to exit, the save option in the dialog will save the file, but then NOT exit the editor.
  the desired behavior is save/save all, but then exit.
- autocomplete for ^KF is terrible, if you type what you want, but there is an autocomplete, you cannot NOT do the autocomplete
  and it always takes an extra enter. we should make <TAB> as indicator that I want to consume the autocomplete
- we need an agent interaction type that represents a "terminal" so that if the agent runs a shell command, we can get a live view
	- example: the python calling toolcall should do this
	- should draw a nice frame around this as well

## 22-05-2026
- the build time is getting long; we should consider splitting the meson
  build in directories with intermediate .a files or something
- our "target X" that we track for crossing shorter lines vertically does not get updated when the user presses the <END> key
  or ctrl-E
	- slighlty annoying interaction issue
- fixed critical systemd-run masking bug where non-existent files in the mask list would prevent the sandbox from starting.
  The code now explicitly checks for file existence before adding them to InaccessiblePaths.
- fixed performance bug where pylsp was started eagerly even for non-python files.
  LSP servers are now started on-demand only when a file of the corresponding language is opened.
- implemented Bracketed Paste Mode to support instant pasting of large text blocks.
  This includes bulk-insert logic in the document and optimized undo recording for paste operations.
- implemented Agent Live Status display in the status bar and agent status window.
  Added a unified status-to-string helper and real-time event triggers for state changes (thinking, tool execution, telemetry updates).
- implemented incremental (streaming) LLM updates and "think" block support.
  Refactored the entire LLM interaction stack (transport, client, and agent) to handle SSE streams, allowing for real-time UI feedback of reasoning and responses.
- implemented `markdown_utils` for automatic table detection and vertical alignment.
  Added a reusable building block for reformatting markdown tables with comprehensive unit testing.
- implemented `code_get_scope`, `code_get_definition`, and `code_get_references` tools.
  These tools provide the agent with semantic code understanding, allowing it to find implementation boundaries and symbol usages across the repository using the LSP.
- implemented background LSP-based enclosing scope tracking.
  The editor now continuously caches the semantic unit (function/class) containing the cursor, which is used to provide precise context for inline agent operations.
- optimized startup by delaying background thread initialization and fixing eager LSP spawning.

## 21-05-2026
- Maybe catch crashdumps and deal with them with gdb nicely, also allows us to give data to the agent in a precooked way
  (maybe a "get_last_crashdump_info" tool - actually get_crashdump_info(nr), and a get_crashdump_list() which returns available crashdumps)
  we need to hook to crashdumpctl and somehow only look at crashdumps from our working space

- mouse support for the file dialog
   - the "recent files" drop thingy is the first candidate
	- crashdump_get_info(nr) and 
	- crashdump_list()
- we need to build a general crashdump tracking infrastructure
    - have a list of crashdumps that come from build and test and run
    - have a window that shows these crashdumps, with a "cursor" so that the user can select a crashdump, hit <enter> and
      get a new window/dialog with details about the crashdump
    - once we have this we can also expose this to the agent
	- crashdump_get_info(nr) and 
	- crashdump_list()
- crashdump tracker.
  - we run systemd-run so crashdumps go into systemd-crashdump, and crashdumpctl will find them
  - we should have an object/list of these somewhere so the user can look at them and we can present them to the agent in a precooked way
  (maybe a "get_last_crashdump_info" tool - actually get_crashdump_info(nr), and a get_crashdump_list() which returns available crashdumps)
  we need to hook to crashdumpctl and somehow only look at crashdumps from our working space
- agent tools
    - formatted subagent completion notification as industry-standard JSON and mounted full logs to the VFS.
- we should take a set of known security sensitive files and in our sandbox, make them disappear
  - .ssh/
  - .env
  - our own API_KEY store
- track which skills got activated and visually mark them as such
	- the initial skill table we report at launch should come with an open box utf8 character (BALLOT BOX: ☐ )
        - once activated we replace that with the same box with a checkmark in it (BALLOT BOX WITH CHECK: ☑ )
- agent tools
    - run_python() with optional code string, file_path, and uv dependency management.
- we need an "AI model" class/extend the current class that tracks URL, model name, purpose, cost (and API key etc). 
    - the agent status window should use this data to calculate the cost 
- have an option in "ai_agent" class to make the whole agent "read only" (to allow for planning mode etc) which
    - rejects any write tool calls (so write tool calls should check agent for read only in various places)
    - we need to decide other security model implications
- we should consider making a ui_listbox UI element and use it in the agent status window
- agent tools
	- list_agents() - returns a markdown table of ID, name, status
 	- create_agent(name, profile) - profile is the .agent.md kind of content
	- agent_status(ID) - returns detailed agent status
	- end_agent(ID)
        - report_my_agent_status(text)
	- agent_todo_status(ID) - returns the todo list with status of a client/sub agent
- CI is currently failing in github, not due to testrun/ missing
 	- needs investigation
- have an attribute on tool call definitions to have it not log by default
   - otherwise tool calls get very verbose for "routine" calls


## 20-05-2026
- subclass the document view for LLM so that we can change the visuals, including fancier rendering of Markdown tables,
  different colors for "think", allow to show terminal output in a subwindow in the document etc, as the agent mode matures
   - we need to make it not a vector of strings, but  vector of "AI elements", which are typed "things" that we can
     decide to render in various ways, some will be multi line, some will be hidden by default. This makes scrolling tricky
     so we likely need to compute and cache Y coordinates for all of these, and have the AI element return its "height"
- we need a window type to show "agent status"
	- "narrow" style window, goal would be the right 20%-30% (TBD) of the screen with the main
	  agent window the rest of the screen
	- all subagents (with a highlight cursor so that we can go view details)
	- activated skills ?
	- active model, tokens consumed, cost$, .... <more over time>
- add python basic syntax highlighting
    - implemented `python_highlighter` with keyword, string, comment, and trailing whitespace support
    - added unit tests and verified with full test suite
- list_tool_calls() tool to return markdown table of all available agent capabilities
  (so goes up to find the start, goes down to find the end)
- add a python LSP (use /usr/bin/pylsp )
    - refactored `clangd_manager` to `lsp_manager` to support multiple language servers
    - added support for starting and routing messages to `pylsp` for `.py` files
- add python basic syntax highlighting
    - implemented `python_highlighter` with keyword, string, comment, and trailing whitespace support
    - added unit tests and verified with full test suite
- list_tool_calls() tool to return markdown table of all available agent capabilities
- sqlite database support
    - implemented `sqlite_create_db`, `sqlite_delete_db`, `sqlite_list_db`, and `sqlite_perform` tools using raw `sqlite3` C API
    - databases are securely stored globally in `~/.cache/turbostar/projects/<hash>/dbs/`
- we need an "AI agent" class/extend the current class that represents a whole agent
    - implemented `ai_agent` class and successfully decoupled it from the `agent_window` UI
- managing todo lists tools (add_todo, list_todos, complete_todo, delete_todo)
    - tools implemented, registered to AI agent state, and heavily E2E tested
- more mouse support -- UI experience item
	- the various save/discard/cancel dialog boxes should take a mouse click on the buttons
	- implemented a new `ui_button` class to encapsulate layout and interaction logic, eliminating hardcoded coordinates.
- can we make next_utf8_character accept a string as argument into which we place the characters
	- the caller should .reserve(4) at least 4 bytes so that in practice we never need to resize/allocate.
	- performance feature: avoids having to construct new objects all the time etc
- our "target X" that we track for crossing shorter lines vertically does not get updated when the user presses the <END> key
  or ctrl-E
	- slighlty annoying interaction issue-E
	- slighlty annoying interaction issue