# short term items (fixes needed -- agents can automatically add todo items to this section) -- not in priority order

- add mouse click interaction on the compaction progress bar to trigger the detailed memory popup dialog (deferred phase)

- track Git HEAD hash in software_map.json to detect codebase churn and dynamically adjust scanning aggressiveness

- perform a thorough security audit of shell argument validation (`fs_utils::is_shell_safe` and callers) to absolutely guarantee no command injection vectors exist via edge-case shell parsing


- we have a set of code reviews in `review*.md` from another agent that at some point we should check
    - the other agent makes stuff up at times, but it is at least worth looking and checking

- github copilot oauth authentication
	- need to read up on this more first how this is supposed to work

- set "uv" working directory: 

- we need to tackle compaction at some point
   -- we're keeping notes in `docs/compaction.md`

- a set of settings (separate dialog!) for a set of tasks, and which model to use for each
	- task 1: summarizing context history
	- task 2: deriving coding style
	- ... more to come over time so we need to make this extensible

- style estimator : look at the current codebase and use clang-format with various options to approximate/detect the coding style (detecting/creating a .clang-format from the codebase if none exists), and then send as a summary to the LLM as part of system prompt. See `docs/design-clang-detect.md` for architecture.

- a "run" option to run the application from the menu, where we temporarily
  exit ncurses (but catch crashes etc) (candidate scanning, project settings, and run configuration dialog are implemented)
    -- gray out the menu item until a main executable name is configured (implemented)
    -- implement the three target execution modes:
        1. Full screen: exit ncurses, run child in its own process group (yield screen/stdin/stdout), catch crashes using libturbocatch.so, prompt "Press any key..." on completion/crash, and resume ncurses. (implemented in sandbox via command_runner)
        2. In a window: create a new terminal window subclass of window with an ANSI terminal emulator.
        3. New X terminal: spawn process in an external X terminal (e.g., xterm) if the DISPLAY environment variable is set.
    -- run the command via gdbserver on a dedicated port to support remote debugging capabilities.

- do we need a whole wrefresh on a cursor move within the screen? or just update the cursor position
   - a "need_cursor_update" flag would be good in addition to need-screen-refresh,
     that was "small" cursor movements don't need a redraw of the content, only the cursor position and status bar

- in the agent interaction, if the result is a markdown table wider than the window, we wrap the table which looks awkward
	- our cut down some wide columns to make things fit

- when a background agent is processing, the cursor flickers badly -- are we redrawing a lot or turning the cursor on/off a lot?
	- not seen recently

- next set of tools for agents
    - next set of tools for agents
      - request-access-to-denied file (to add to the security manager, will ask the user)
    	- gdbserver -- allow interactive debug of an app (especially a crash) by the LLM
	        - read memory, get registers
    - crashdump; 
	- crashdump_read_memory(nr, location, size)
	- crashdump_gdb(nr, command) (over time -- likely to come later)
- sandbox: we should provide the agent a scratch directory space (tmpfs backed) that is explicitly allowed for
  write in the tool security system and sandbox system so that the agent does not need to clobber the actual
  project directory with small python or other scripts it makes to do things

- syntax highlighting of trailing whitespace is annoying if you're still typing the line.. any space you type
   instantly turns red. Need to maybe know which line the cursor is on or something, or wait for 10 seconds or .. or ..
	maybe we need to delay any syntax coloring/checking update until no typing happened for a couple of seconds or hit enter/change Y cursor line
   - not urgent and needs more thought



- if we have warnings/etc info, the initial system prompt should tell the agent that, or maybe it's an early notification
    - at the end of a compile and there are errors or warnings, we need a system notification to the agent that there is new info
    - low priority


- better undo batching than "every keypress"


# mid term items

- gdbserver notes on how to debug an application nicely
	terminal 1:	gdbserver :1234 ./my_program
	terminal 2:	gdb ./my_program
			target remote localhost:1234
			continue (etc)


- a github:// VFS namespace
	- mostly maps to just downloading the raw file with a small LRU driven cache
	- readdir is the complex one, both for projects and for files in a project


- mouse support for resizing windows (bottom right corner) and moving (title bar)

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