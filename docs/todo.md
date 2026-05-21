# short term items (fixes needed -- agents can automatically add todo items to this section) in random order
 
- do we need a whole wrefresh on a cursor move within the screen? or just update the cursor position
   - a "need_cursor_update" flag would be good in addition to need-screen-refresh,
     that was "small" cursor movements don't need a redraw of the content, only the cursor position and status bar

- we need an "AI model" class that tracks url, model name, purpose, cost, api key for each model
   - over time we can use this to switch models dynamically
   - we should query a set of API points for their supported models at startup
   - need a small "we know these model names" database compiled into the binary from some docs/*.md file
     so that we can fill in purpose, and outright ban bad models
   - likely allow .turbostar like files to append to the build in list

- we should take a set of known security sensitive files and in our sandbox, make them disappear
  - .ssh/
  - .env
  - our own API_KEY store

- we need a window type to show "agent status"
	- "narrow" style window, goal would be the right 20%-30% (TBD) of the screen with the main
	  agent window the rest of the screen
	- all subagents (with a highlight cursor so that we can go view details)
	- activated skills ?
	- active model, tokens consumed, cost$, .... <more over time>

- next set of tools for agents
    - request-access-to-denied file (to add to the security manager, will ask the user)
    - run_python() -- start with filename as argument -- maybe allow direct python snippets as well
	- also allow "modules to install" as parameter so we can pass those to "uv", solves the "system pip" problem
	- we should consider always using "uv" if it is available and fall back to python3 if not
	- the benefit of direct python code is that we can put it somewhere clever and the agent does not 
	  need to clobber the project for it.
    - a set of LSP tools to help code navigation
	- code_find_definition
	- code_find_references
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
	- git_commit(message)
	- git_create_pr(message)
    - gdbserver -- allow interactive debug of an app (especially a crash) by the LLM
        - read memory, get registers
    - web_fetch(URI)
	- need permission manager for which domains the user has allowed - stored in a new config file somewhere
	- needs a way to ask the user for permission
        - probably a popen to /usr/bin/curl as that should get all the https certs right
    - coredump; 
	- coredump_get_info(nr) and 
	- coredump_list()
	- coredump_read_memory(nr, location, size)
	- coredump_gdb(nr, command) (over time -- likely to come later)

- sandbox: we should provide the agent a scratch directory space (tmpfs backed) that is explicitly allowed for
  write in the tool security system and sandbox system so that the agent does not need to clobber the actual
  project directory with small python or other scripts it makes to do things

- syntax highlighting of trailing whitespace is annoying if you're still typing the line.. any space you type
   instantly turns red. Need to maybe know which line the cursor is on or something, or wait for 10 seconds or .. or ..
	maybe we need to delay any syntax coloring/checking update until no typing happened for a couple of seconds or hit enter/change Y cursor line
   - not urgent and needs more thought

- track which skills got activated and visually mark them as such
	- the initial skill table we report at launch should come with an open box utf8 character (BALLOT BOX: ☐ )
        - once activated we replace that with the same box with a checkmark in it (BALLOT BOX WITH CHECK: ☑ )

- should we send the initial system prompt and tool info as we open the agent window and not wait for the first user prompt?
	- goal: reduce latency for first actual prompt

- we need to build a general coredump tracking infrastructure
    - have a list of coredumps that come from build and test and run
    - have a window that shows these coredumps, with a "cursor" so that the user can select a coredump, hit <enter> and
      get a new window/dialog with details about the coredump
    - once we have this we can also expose this to the agent

- "Spell check document" option in the Agent window that just runs a prompt and updates the document error list

- incremental (think) updates from the LLM (needs a different protocol flow throughout the whole system - not a small task)

- subclass the document view for LLM so that we can change the visuals, including fancier rendering of Markdown tables,
  different colors for "think", allow to show terminal output in a subwindow in the document etc, as the agent mode matures
   - we need to make it not a vector of strings, but  vector of "AI elements", which are typed "things" that we can
     decide to render in various ways, some will be multi line, some will be hidden by default. This makes scrolling tricky
     so we likely need to compute and cache Y coordinates for all of these, and have the AI element return its "height"


- a function that works on a document, with start and finish lines as argument, and aligns markdown table | lines vertically
   - step 1: track widest field on a per column basis, this becomes the target width for that column - but ignores extra padding
   - step 2: pad all cells to their target width so that all | are perfectly vertically aligned

- our "reformat selection" hotkey for markdown could combine the above 2 steps somehow

- a function that works on a document, that first aligns a markdown table (with the function above), and then replaces 
  the border characters with pretty UTF8 single line borders
	- this means adding 2 lines, one for above and one for below the markdown lines

- A "Run" menu command in the tools menu, it runs the application where the editor leaves the whole screen for the app until it exits, 
  or we launch a new terminal if DISPLAY/etc are set. Need to run it with our systemd-run wrapper so we can collect 
  any coredumps easily

- Maybe catch coredumps and deal with them with gdb nicely, also allows us to give data to the agent in a precooked way
  (maybe a "get_last_coredump_info" tool - actually get_coredump_info(nr), and a get_coredump_list() which returns available coredumps)
  we need to hook to coredumpctl and somehow only look at coredumps from our working space

- enhance syntax highlighting -- support a few more things in C++ with reasonable colors


- spell check via an LLM call with a good prompt, asking the LLM to flag errors
   "Check this document for spelling and gramatical errors, and use the flag_as_error tool to report any spelling mistake and severe gramatical mistake as error and flag gramatical improvements as warning"
   this can be a temporary new agent connection, that destructs after the agent is done with the spell check

- paste speed is somehow artificially limited, you can almost see the characters beeing typed
  while other editors are instant. Are we repainting every key press always?
   - we need to use Bracketed Paste Mode by printing printf("\033[?2004h"); at start and printf("\033[?2004l"); at exit
	- maybe ncurses has a nice abstraction for this and the next listed sequences
   - this causes \x1b[200~ to be entered just before the paste starts (which we need to eat) and \x1b[201~
     at the end of the paste. While we're in this "in the paste" mode we should surpress screen updates,
     and just do one final refresh when getting the end-of-paste marker.
   - this a real UI interaction issue, performance is terrible even now that our drawing speed is much better
   - on receiving the 200 message, we may want to wait, say, 10msec for all input from the paste to arrive, and then bulk
     process.


# mid term items

- mouse support for the file dialog
   - the "recent files" drop thingy is the first candidate

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

## 21-05-2026
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