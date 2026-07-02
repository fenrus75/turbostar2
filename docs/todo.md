# Web page todos (in `docs/index.html`, `docs/ai.html` and `docs/editor.html`)

## Webpage Screenshots to Take
- [ ] Screenshot of the website homepage running in a browser showing the retro-modern Turbo Pascal HSL color scheme
- [ ] Screenshot of the compile context optimizer output in the terminal showing raw build logs vs. minimized diagnostics
- [ ] Screenshot of an agent interactive debugging session with parallel GDB execution and stdin interactions
- [ ] Screenshot of an inline agent running automated security scans via `bandit` on a C++/Python file

## Webpage todo items (do not delete this header line)
- [ ] Editor: Add a section about our built-in hex editor, we have a screenshot for this in `docs/hexeditor.png`


# short term fixes -- not in priority order, agents can add and remove items as they come up (do not delete this header line)


- usability: "prompt-from-file" -- simple clickable thing in the agent window that loads the content of a file into the multi-line edit
   field for the prompt  -- better option: support ^K mode in multiline edit, with "R" for reading from file

- agent prompt (multi-line edit) -- have a cursor-up/down way of repeating older prompts (so shell-like history)

- somehow do python plugins for agents and tool calls?

- subagent creation wizzard dialog

- model aliases -- have a set of aliases we can map to actual models, and we can have our "Task Models" UI to select models 
   for specific tasks set up these aliases.
	- base
	- plan
	- review
	- code
	- image

- feature: add a /rescan TUI slash command/shortcut to hot-reload custom subagents inside subagent_manager during runtime.

- become an A2A server for our agents

- become a client to A2A
	we need to parse agent cards and hook them up to our subagent directory

- build an A2A directory
	- maybe a .local style directory service? is there a protocol for autodiscovery?

- have a turboserver mode where we are headless but only serve A2A requests?

- we need a /command registery so that plugins can register commands
	- will clean up the current handling a lot
	- will need to decide what kind of things we need to pass to the handler
		- agent_
		- ...

	- a registered /command action that activates a skill
	- a registered /command action that does a toolcall


- a /command action that activates a tool family (via menu?)

- a /command action for /clear of agent history -- basically a new connection

- separate model name database of famous models for default properties
  should investigate the compile time json-to-struct stuff
	- https://raw.githubusercontent.com/BerriAI/litellm/refs/heads/litellm_internal_staging/model_prices_and_context_window.json

- separate model provider (server) database for easy population of model servers
	- need a smart combo box or radio box for "from database" vs "custom" so that it is easy to add either

- feature: have a separate model option for "plan mode" phase

- feature: similar to "run with gdb" we should have a "run with perf record" so that the agent can natively do performance analysis/get perf data
	- need to check the sysfs to see if this is available, if not just hide it entirely
	- perf stat summary?
	- hot functions
	- fs_read_lines option to get per line perf data?

- code review enhancements
	- we can provide upfront a set of static analysis data to the code review agent
		- known compile / LCP warnings
		- cyclic complexity scores?
		- cpplint or other similar tools
		- -fanalyzer output ? should maybe add to fs_compile_file when in code review agent
		- for python we have our security analyzer -- we should run this when reviewing python files
		- if we had backtraces/crashes that involve this file we should include them in context or at least reference them with the toolcall to get the details


- we should create a code review agent file in agents/

- the code review agent should have access to the security plugin tool namespace

- the git_ family of tools should be in a git tool namespace, and this should be in the normal baseline tool set

- the "view review items" overview box is still terrible due to lack of working word wrap on long lines -- we may need to just cut these off instead?

- meta feature: helper agents
	- well rounded subagents that have custom tools available to it for specific higher level tasks, can be used by the main agent as if they are fancy tool calls
		- we started this with code review kind of
	- should be able to suggest model names/capabilities to be run in -- say a vision model for image processing tasks
	- should be able to register /slash commands
	- skills that plug into specific subagent types only
	- should have prefered color set + logo for the grid view


	- image processing
		- file format conversion
		- cut out parts of image into new image (crop)
		- rescale image / rotate / ..
		- to grayscale conversion
		- basic filters? gimplib?
		- USB camera access
		- opencv segmentation/yolo/etc done local


	- file format conversion
		- PDF to XYZ
		- OCR

	- english grammer/spelling/clarity

	- markdown toolbox

	- performance
		- linux perf integration
		- intel performance skill integration

	- homeassistent / home automation

	- code review

	- security review
		- plug in static local scanners
		- maybe fuzzing helpers?

	- fpga /verilog agent?



- better integration for code coverage in the main editor
	- color coding in the left window decoration?
	- or color in the editor window with a view mode with a toggle key

- bug: The agent window text renderer silently truncates large blocks of concatenated system messages. Specifically, when multiple system messages merge into the same visual turn (e.g., initial system prompt + `/save` outputs or `/help` outputs), `wrap_text` or `markdown_utils::align_all_tables` deletes the text between the top few lines and the bottom few lines. This caused the E2E mouse scrolling test to fail because the chat history was artificially shortened.

- since we have github:// and skills://
	- we could add skills by just a git hub url somehow clever so no need for local storage
	- useful for domain activated skills say in the x86 namespace

- Feature: MCP server: if the mcp server is in a directory that has a .git, can we check if there's an update upstream (github?)
      - we could build an auto-update feature!


- feature: a "desired_format" optional argument to web_fetch that behind the scenes calls various format converters, example pdf to markdown
	- alternative: a convert_file_format() tool call
	- need to do pro/con between these options

- SSH forwarded X11 sessions do not work well in our sandbox

- bug: valgrind does not work in our sandbox

- visual: in the agent interaction, if the result is a markdown table wider than the window, we wrap the table which looks awkward
	- need to just spill to the right instead?
	- or cut down some wide columns to make things fit (replace super long fields with "....")

- sandbox: we should provide the agent a scratch directory space (tmpfs backed) that is explicitly allowed for
  write in the tool security system and sandbox system so that the agent does not need to clobber the actual
  project directory with small python or other scripts it makes to do things

- MCP support enhancements
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

- wayland virtual server so that gui apps can be debugger cleverly 


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

## 02-07-2026
- implemented F5 key shortcut to pop up a multiline prompt in a full editor window named `*Prompt*` with real-time bidirectional synchronization.
- implemented command line option `--force-ascii` and tristate file type check (`ASCII`, `MAYBE`, `BINARY`) to prompt the user when opening files with null bytes.
- implemented timeout parameters for shell command, python, testing, and compile tool execution, and added a default 60s timeout for git commands via `execute_command_sync`.
- extended the `fs_grep_files` tool to perform an LSP symbol search on regular word queries, returning their definition locations formatted with "is defined in" at the top of the output.

## 28-06-2026
- implemented an HTML verifier tool (security_verify_html) for the securityagent plugin using "tidy" (if tidy exists).
- implemented syntax highlighting colors configuration manager and dynamic colors.json persistence, and created an interactive side-by-side color picker dialog in the editor options menu.
- agent skills
- updated AI page with a default agent integration screenshot and introduction section.
- updated AI page virtual context paging section with the runs-testsuite screenshot, and added a dedicated section describing the integrated agent undo history.
- added Claude-style agent definitions and dynamic subagents card to the Standards & Extensibility grid on the AI page.

## 27-06-2026
- integrated the screenshot of the Crash Catcher and Manager interface (screenshot-crash-manager.png) into the website.
- merged the Crash Catcher and Core View Manager website sections under a single combined section with one unified screenshot, making `screenshot-core-view-manager.png` obsolete.
- fixed the core dump details viewer screen (crashdump_window) to use the yellow-on-blue color scheme and restored active ncurses color attributes after the listbox draws.
- updated the website's compile/debug section to focus on "Developer Native: Compile, Run & Debug" showing that applications can be executed directly within the editor.
- enhanced the semgrep tool in the securityagent plugin to support scanning HTML files using a custom command line with '--config auto' and '--include="*.html"'.
- integrated markdown_utils::align_all_tables in crashdump_window to pretty-print and align markdown tables in crash reports automatically.
- implemented the webpage image overlay (lightbox) feature in JS/CSS so that clicking screenshot links shows a smooth zoomed-in modal in the current tab instead of opening in a new tab.
- integrated yaml-cpp to parse SKILL.md headers fully and conformantly instead of using manual line scanning.