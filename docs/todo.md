# short term items (fixes needed -- agents can automatically add todo items to this section) in random order

- do we need a whole wrefresh on a cursor move within the screen? or just update the cursor position
   - a "need_cursor_update" flag would be good in addition to need-screen-refresh,
     that was "small" cursor movements don't need a redraw of the content, only the cursor position and status bar


- pass the build/ directory to the clangd LSP so it knows where to find compile_commands.json

- we don't pass -pty or equivalent to systemd-run which means we don't see the output of a compile happening

- we need an agent interaction type that represents a "terminal" so that if the agent runs a shell command, we can get a live view
	- example: the python calling toolcall should do this
	- should draw a nice frame around this as well

- autocomplete for ^KF is terrible, if you type what you want, but there is an autocomplete, you cannot NOT do the autocomplete
  and it always takes an extra enter. we should make <TAB> as indicator that I want to consume the autocomplete

- when a background agent is processing, the cursor flickers badly -- are we redrawing a lot or turning the cursor on/off a lot?

- implement a generic "diff view" that can visualize the changes in the last undo group.
  This is essential for reviewing automated edits from inline agent operations.

- next set of tools for agents
    - a fs_mkdir() call to allow the agent to make subdirectories -- should act like "mkdir -p", so recursive
    - request-access-to-denied file (to add to the security manager, will ask the user)
    - a set of LSP tools to help code navigation
	- code_lsp_rename
    - a set of git ops
	- git_branch(branchname)
	- git_checkout(...)
	- git_pull()
	- git_push(force) -- if fource is specified we need to ask the user permission -- maybe always decline for now
	- git_add(list of filenames)
	- git_diff_from_branch(branchname)
	- git_diff (uncommitted changes list)
	- git_status()
	- git "what is staged"  (git --no-page diff --stat)
	- git_commit(message)
	- git_create_pr(message)
    - gdbserver -- allow interactive debug of an app (especially a crash) by the LLM
        - read memory, get registers
    - web_fetch(URI)
	- need permission manager for which domains the user has allowed - stored in a new config file somewhere
	- needs a way to ask the user for permission
        - probably a popen to /usr/bin/curl as that should get all the https certs right
    - coredump; 
	- coredump_read_memory(nr, location, size)
	- coredump_gdb(nr, command) (over time -- likely to come later)
    - enter_plan_mode, exit_plan_mode
    - run_shell_command

- sandbox: we should provide the agent a scratch directory space (tmpfs backed) that is explicitly allowed for
  write in the tool security system and sandbox system so that the agent does not need to clobber the actual
  project directory with small python or other scripts it makes to do things

- syntax highlighting of trailing whitespace is annoying if you're still typing the line.. any space you type
   instantly turns red. Need to maybe know which line the cursor is on or something, or wait for 10 seconds or .. or ..
	maybe we need to delay any syntax coloring/checking update until no typing happened for a couple of seconds or hit enter/change Y cursor line
   - not urgent and needs more thought

- should we send the initial system prompt and tool info as we open the agent window and not wait for the first user prompt?
	- goal: reduce latency for first actual prompt


- "Spell check document" option in the Agent window that just runs a prompt and updates the document error list
	- this would be a great addition to our ^P menu, to spell check (and fix) text (for non code) or comments and user strings (for code)

- if we have warnings/etc info, the initial system prompt should tell the agent that, or maybe it's an early notification
    - at the end of a compile and there are errors or warnings, we need a system notification to the agent that there is new info




# mid term items

- the whole coredump approach needs a rethink, the world is 100x more complex than assumed

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
- Maybe catch coredumps and deal with them with gdb nicely, also allows us to give data to the agent in a precooked way
  (maybe a "get_last_coredump_info" tool - actually get_coredump_info(nr), and a get_coredump_list() which returns available coredumps)
  we need to hook to coredumpctl and somehow only look at coredumps from our working space

- mouse support for the file dialog
   - the "recent files" drop thingy is the first candidate
	- coredump_get_info(nr) and 
	- coredump_list()
- we need to build a general coredump tracking infrastructure
    - have a list of coredumps that come from build and test and run
    - have a window that shows these coredumps, with a "cursor" so that the user can select a coredump, hit <enter> and
      get a new window/dialog with details about the coredump
    - once we have this we can also expose this to the agent
	- coredump_get_info(nr) and 
	- coredump_list()
- coredump tracker.
  - we run systemd-run so coredumps go into systemd-coredump, and coredumpctl will find them
  - we should have an object/list of these somewhere so the user can look at them and we can present them to the agent in a precooked way
  (maybe a "get_last_coredump_info" tool - actually get_coredump_info(nr), and a get_coredump_list() which returns available coredumps)
  we need to hook to coredumpctl and somehow only look at coredumps from our working space
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
	- slighlty annoying interaction issue