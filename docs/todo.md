# Web page todos (in `docs/index.html`, `docs/ai.html` and `docs/editor.html`)

## Webpage Screenshots to Take
- [ ] Screenshot of the website homepage running in a browser showing the retro-modern Turbo Pascal HSL color scheme
- [ ] Screenshot of the compile context optimizer output in the terminal showing raw build logs vs. minimized diagnostics
- [ ] Screenshot of an agent interactive debugging session with parallel GDB execution and stdin interactions
- [ ] Screenshot of an inline agent running automated security scans via `bandit` on a C++/Python file
- [x] Screenshot of in-editor CPU performance profiling heatmap and F7 hotspot navigation for `docs/editor.html` (`docs/screenshot-perf-profiling.png`)
- [x] Screenshot of AI agent running CPU performance profiling summary & detail tools for `docs/ai.html` (`docs/screenshot-agent-performance-profile.png`)

## Webpage todo items (do not delete this header line)


## Style note for the website
remember to describe features in terms of the benefit to the user or the agent, not the implementation details

# short term fixes -- not in priority order, agents can add and remove items as they come up (do not delete this header line)

- exit plan mode is rather terrible -- the plan does not scroll in the dialog nor can it open it in a document

- fs_run_tests also needs to run the meson filter

- our own testlist is hindered by our sandbox - we may need to open it a bit? uv fails for example

- fs_edit_lines -- code after edits may be large! need to ponder what to do

- we need to allow for plugin settings somehow, to ask for API keys and such
	- maybe just allow string <-> string settings (so a std::map basically)
		- well we need tupple: plugin, name -> value, so it's slightly more complex
			- we could encode as plugin:name
		- ideally we have a display priority/order integer as well so we can sort the UI into a logical order
	- plugins can register and ask for a setting
	- the TUI will build tabs per "plugin"
		- then sorts the fields by priority, then name
		- then builds input fields
	- persistence: a plugin-settings.json file per project/global?

- agent connection keepalive -- if the request takes a long time, is there a way to do a keepalive to keep the connection from dropping

- 1. **Highlight Differences (Hex Diffing)**:
   * *The Idea*: Add a companion tool `hexdiff` that takes two file paths, compares them, and highlights only the changed bytes along with
	their structural roles. This would be incredibly useful for verifying binary patches or analyzing compiler optimization impacts.
	- doing this right is VERY complicated, we would need the bsdiff algorithm and then expand the result in an agent
	  friendly format

- 3. **Interactive Byte Editor Interface**:
	   * *The Idea*: A high-level visual representation of the hex edits in the editor UI (similar to how `flag_as_error` creates an overlay)
	would help developers track what modifications the agent is proposing in binary formats.

- nit: the hexeditor has a size limit that's a bit on the small side -- maybe we should check system memory size and on large systems
    increase the limits?
	- should we maybe mmap() the original file?

- feature: subagent creation wizzard dialog

- feature: model aliases -- have a set of aliases we can map to actual models, and we can have our "Task Models" UI to select models 
   for specific tasks set up these aliases. We have a preferences UI for this now! We just need to get to a good list!
	- base
	- plan
	- review
	- code
	- image
	- small-and-fast?
	- local

- feature: have a separate model option for "plan mode" phase
	- same feature as the aliases feature

- feature:a /command action that activates a tool family (via menu?)

- feature: separate model name database of famous models for default properties
  should investigate the compile time json-to-struct stuff
	- https://raw.githubusercontent.com/BerriAI/litellm/refs/heads/litellm_internal_staging/model_prices_and_context_window.json

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
	- 

- bug: the "view review items" overview box is still terrible due to lack of working word wrap on long lines -- we may need to just cut these off instead?
	- should we kill the feature instead?

- meta feature: helper agents
	- well rounded subagents that have custom tools available to it for specific higher level tasks, can be used by the main agent as if they are fancy tool calls
		- we started this with code review kind of
	- should be able to suggest model names/capabilities to be run in -- say a vision model for image processing tasks
	- [x] should be able to register /slash commands
	- skills that plug into specific subagent types only
	- should have prefered color set + logo for the grid view


	- image processing
		- [x] file format conversion
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

	- homeassistent / home automation
		- needs the plugin config management for asking the API cookie

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

- PDF Stream Extractor Helper (`pdf_extract_stream`):
  - *The Idea*: Add a higher-level tool or option that locates a PDF object by its ID (e.g. `1 0 obj`), parses the PDF XREF table, extracts its `/Length`, `/Filter`, and stream offsets, and automatically decompresses it, rather than requiring manual hexdump offset checks.




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


# done items (sorted by date, newest first)

## 31-07-2026
- Build Optimization for `git_version.h` & `crash_handler.cpp` (`src/meson.build`, `meson.build`, `docs/todo.md`): Decoupled `src/crash_handler.cpp` from `core_sources` and `libturbostar_core.a`. `src/crash_handler.cpp` is the sole consumer of `git_version.h` (generated on every Git commit). Previously, including it in `libturbostar_core.a` caused all 164 unit test binaries to re-link on every git commit. Moving `src/crash_handler.cpp` and `git_version_h` to standalone executable sources (`turbostar` and `test_fallback_crash`) reduced post-commit rebuild re-linking from ~164 binaries to 1 binary (< 1s execution).
- Bearer Token Authentication for Turboserver (`src/a2a/a2a_server.h/cpp`, `src/config_manager.h/cpp`, `src/main.cpp`, `src/ui/dialog_factories.cpp`, `tests/unit/test_a2a_server.cpp`): Added Bearer token authentication support to `turboserver` (A2A server mode). Configured `~/.turboserver` storage (POSIX `0600` permissions) with `token` and `enforce_token` keys, auto-generating a secure random token if empty. Added `--a2a-token` and `--a2a-enforce-token` CLI flags and `TURBOSERVER_TOKEN` / `TURBOSERVER_ENFORCE_TOKEN` environment variable overrides. Implemented HTTP `401 Unauthorized` pre-routing enforcement in `a2a_server::setup_routes()` and integrated token configuration into the TUI Preferences dialog. Added unit tests for valid, invalid, and missing Bearer tokens.
## 31-07-2026
- TUI Options Menu Restructuring & Interactive Controls Restoration (`src/ui/menu_bar.cpp`, `src/event_queue.h`, `src/editor_events.cpp`, `src/editor_events_ui.cpp`, `src/editor_events_key.cpp`, `src/config_manager.h/cpp`, `src/ui/dialog_factories.h/cpp`, `tests/unit/test_options_dialogs.cpp`): Consolidated the Options menu from 7 scattered items down to 3 unified tabbed dialog entries (`Editor Settings...`, `AI & Agent Settings...`, `A2A & Remote Settings...`). Embedded full interactive sub-controls and action buttons across all tabs: (1) **Editor Settings** embeds live `Syntax Colors` editing (`ui_listbox` attribute list + `ui_color_picker`), (2) **AI & Agent Settings** adds `[ Browse... ]` model selection dialog trigger, LLM Provider management (`[ Add ]`, `[ Edit ]`, `[ Delete ]` servers), expanded Task-to-Model dropdowns (Chat, Summary, Reviewer, Verifier, Planning, Coding, Fast), and MCP server toggle controls (`[ Toggle Enable ]`), (3) **A2A & Remote Settings** adds `[ Generate ]` bearer token button and remote A2A server management (`[ Add ]`, `[ Edit ]`, `[ Delete ]`). Updated `editor_events_key.cpp` with sub-dialog routing. Verified with 100% test pass.
- `ui_tabbed_container` Component (`src/ui/components/ui_tabbed_container.h/cpp`, `src/ui/ui_element.h`, `tests/unit/test_ui_tabbed_container.cpp`): Designed and implemented `ui_tabbed_container`, a composite container widget for category-based tabbed UI dialog layouts. Renders a left-aligned vertical selection sidebar with auto-calculated width (minimum 14, soft maximum guideline 22 chars), a Turbo Pascal style vertical border (`│`), and active page content routing. Form values are seamlessly queried across all tabs via `get_value()`. Added `ui_tabbed_container` to parent subclass table in `ui_element.h` and added unit test suite in `tests/unit/test_ui_tabbed_container.cpp`. Verified with 100% test pass.

## 30-07-2026
- Git Remote URL & Branch System Prompt Context (`src/git_manager.h/cpp`, `src/project_manager.cpp`, `src/tools/invoke_subagent/invoke_subagent_entry.cpp`): Added `get_remote_origin_url()` and `get_current_branch()` methods to `git_manager`. Updated `project_manager::get_project_knowledge_prompt()` to automatically inject the project's upstream Git repository URL and active branch into system prompt context across all agent types (main agent window, subagents, code review agents, security agents), so local agents know the repository URL and branch when delegating tasks to remote A2A subagents. Updated `invoke_subagent` auto-detection logic. Verified with unit test suite.
- Tool Call Aliases Hashmap Refactoring (`src/agentlib/llm_types.h`): Refactored the tool call alias name resolution in `normalize_tool_call()` from a long `if-else if` chain into a clean static `std::unordered_map<std::string_view, std::string_view>` lookup table (`tool_name_aliases`). Removed redundant 1:1 self-mappings and verified with unit test suite (`test_tool_infrastructure`).
- Remote A2A Workspace Git Worktree Mode (`src/a2a/a2a_server.h/cpp`, `src/agentlib/subagent_manager.h/cpp`, `src/main.cpp`, `tests/unit/test_a2a_server.cpp`): Added `--git-worktree` / `--a2a-worktree` CLI flag to `turboserver`. When enabled, `a2a_server` creates a zero-latency private git worktree checkout (`git worktree add -f <task_dir> HEAD`) directly from the server's local repository for incoming tasks, automatically removing and pruning the worktree upon task completion. When worktree mode is active, `repository_url` and `git_ref` are automatically omitted from published Agent Card `input_schema` definitions since workspace pre-seeding is handled locally. Thread-protected `subagent_manager` access with `std::shared_mutex`.
- Remote A2A Workspace Repository Pre-Seeding (`src/a2a/a2a_server.cpp`, `src/a2a/a2a_client.h/cpp`, `src/tools/invoke_subagent/`, `src/agentlib/subagent_manager.cpp`, `docs/tools.md`): Added support for `repository_url` and `git_ref` parameters in `invoke_subagent` and `/a2a/v1/agents/{agent}/tasks` payloads. When an A2A task is submitted with a repository URL (or auto-detected from the active project's `git config remote.origin.url`), `a2a_server` clones the repository (`git clone --depth 1 [--branch ref]`) into the isolated task workspace directory prior to starting the remote subagent. Added parameter schemas to synthesized A2A agent cards (`input_schema`) and updated documentation. Verified with unit test suite.
- Unit Test A2A HOME Environment Isolation Fix (`tests/unit/test_a2a_client.cpp`, `tests/unit/test_a2a_servers_dialog.cpp`, `tests/unit/test_a2a_connect_server.cpp`): Isolated `HOME` environment variable to dedicated temporary directories (`test_a2a_client_home`, `test_a2a_dlg_home`, `test_a2a_conn_home`) across A2A unit tests. Previously, running unit tests without `HOME` isolation caused `save_global_servers()` in `test_a2a_client.cpp` to write test server entries (`gpu_box`) into the user's actual `~/.cache/turbostar/a2a_servers.json` configuration file. Cleaned global configuration file and verified tests.
- A2A Server Manager Startup Initialization Fix (`src/main.cpp`): Added explicit `a2a_server_manager::get_instance().initialize()` call during application startup in `src/main.cpp` after `project_manager::get_instance().initialize()`. Previously, `load_global_servers()` and `load_project_servers()` were never triggered on application boot, causing saved A2A server configurations in `.cache/turbostar/projects/<hash>/a2a_servers.json` to not be loaded into memory.
- TUI A2A Server Edit Direct State Access Fix (`src/ui/dialog_factories.cpp`, `src/editor_events_key.cpp`): Fixed server URL corruption where `create_a2a_server_edit_dialog()` serialized values with colon separators (`save:name:url:auth`), causing URLs with colons (e.g., `http://devpc.fenrus.org:7820/`) to be improperly split into `url="http"` and `auth="//devpc.fenrus.org:7820/"`. Updated `editor_events_key.cpp` to directly read field values from `active_dialog_->get_value()` when confirmed, eliminating colon-delimiter formatting and parsing errors.
- TUI A2A Server Edit Button (`src/ui/dialog_factories.h/cpp`, `src/editor_events_key.cpp`, `tests/unit/test_a2a_servers_dialog.cpp`): Added `Edit...` button (`'e'`) to `create_a2a_servers_dialog()` enabling existing remote A2A server configurations to be modified interactively. Updated `create_a2a_server_edit_dialog()` to accept pre-populated `initial_auth` token parameters and contextually set dialog titles to "Add A2A Server" or "Edit A2A Server". Added `edit:` result routing handler in `src/editor_events_key.cpp` and unit test cases in `tests/unit/test_a2a_servers_dialog.cpp`. Verified with unit test suite.
- MCP Server Shutdown Pipe Close Fix (`src/mcp/mcp_server.cpp`): Fixed application hang/SIGABRT during shutdown where `mcp_server::stop()` held `stdout_fd_` and `stderr_fd_` open while waiting for process termination with `waitpid()`, leaving background `reader_loop()` blocked inside synchronous `read(stdout_fd_)` and preventing `reader_thread_.join()` from completing. Moving pipe file descriptor closures before process wait unblocks worker threads immediately upon shutdown.
- TUI A2A Remote Server Manager Dialog (`src/ui/dialog_factories.h/cpp`, `src/ui/menu_bar.cpp`, `src/editor.h`, `src/event_queue.h`, `src/editor_events.cpp`, `src/editor_events_ui.cpp`, `src/editor_events_key.cpp`, `tests/unit/test_a2a_servers_dialog.cpp`): Designed and implemented `create_a2a_servers_dialog()` and `create_a2a_server_edit_dialog()` providing an interactive Turbo Pascal 7 style TUI window for managing remote A2A server connections. Added `A2A Servers...` item under Options menu (`event_type::a2a_config`), added central event routing dispatch in `src/editor_events.cpp`, and implemented dialog action handlers (`Add...`, `Test/Card`, `Remove`). Updated `src/ui/dialog.h` with `get_title()` getter and added unit test suite in `tests/unit/test_a2a_servers_dialog.cpp`. Verified with 100% test pass (253 OK).
- A2A Server Dynamic Connection Tool & `--a2a-connect` CLI Flag (`src/tools/a2a_connect_server/`, `src/main.cpp`, `src/tools/meson.build`, `meson.build`, `docs/tools.md`, `tests/unit/test_a2a_connect_server.cpp`): Implemented `a2a_connect_server` tool enabling dynamic connection and registration of remote A2A servers at runtime. Supports optional bearer tokens, agent card fetching/skill summary, and `persistent: true` saving to project-local settings (`.cache/turbostar/projects/<hash>/a2a_servers.json`). Added `--a2a-connect <name>=<url>` CLI command-line argument to `src/main.cpp` for declaring ephemeral A2A server connections upon application startup. Documented tool schema in `docs/tools.md` and added unit test suite in `tests/unit/test_a2a_connect_server.cpp`. Verified with 100% test pass (251 OK).
- A2A Remote Subagent Routing & `invoke_subagent` Integration (`src/tools/invoke_subagent/invoke_subagent_security.cpp`, `src/tools/invoke_subagent/invoke_subagent_entry.cpp`, `tests/unit/test_invoke_subagent.cpp`): Wired `invoke_subagent` tool to route remote A2A subagent invocations using `server_name:agent_name` prefix syntax (e.g. `devpc:research`). Enforced `local_only: true` validation that rejects remote server calls when `local_only` is true. Resolves server names against `a2a_server_manager` and delegates asynchronous task submission or synchronous execution/polling through `a2a_client`. Updated unit test cases in `tests/unit/test_invoke_subagent.cpp`. Verified with 100% test pass (250 OK).
- A2A Client Infrastructure & 3-Tier Server Manager (`src/a2a/a2a_server_manager.h/cpp`, `src/a2a/a2a_client.h/cpp`, `src/meson.build`, `meson.build`, `tests/unit/test_a2a_client.cpp`): Implemented A2A Client core infrastructure. Created `a2a_server_manager` with 3-tier priority server configuration merging (Ephemeral Runtime > Project-Local `.cache/turbostar/projects/<hash>/a2a_servers.json` via `fs_utils::get_project_cache_root()` > Global-System `.cache/turbostar/a2a_servers.json` via `fs_utils::get_global_cache_dir()`). Created `a2a_client` handling remote agent discovery (`fetch_agent_card`), task queuing (`submit_task`), polling (`poll_task`), synchronous execution (`execute_task_sync`), and task cancellation (`cancel_task`). Updated `src/meson.build` and `meson.build`, and added unit test suite in `tests/unit/test_a2a_client.cpp`. Verified with 100% test pass (251 OK).
- Subagent Orchestration Tool Suite Renaming (`src/tools/`, `src/agentlib/llm_types.h`, `docs/tools.md`, `tests/unit/`, `tests/e2e/`): Renamed subagent orchestration tools to match industry-standard naming conventions: `create_agent` $\to$ `invoke_subagent` (with `local_only` boolean parameter), `message_agent` $\to$ `send_message`, `list_agents` $\to$ `list_subagents`, `agent_status` $\to$ `get_subagent_status`, `wait_for_agent` $\to$ `wait_for_subagent`, `agent_get_output` $\to$ `get_subagent_output`, `agent_report_final_result` $\to$ `report_final_result`, `end_agent` $\to$ `kill_subagent`, and `agent_todo_status` $\to$ `get_subagent_todo_status`. Added compatibility alias resolution in `src/agentlib/llm_types.h` (`normalize_tool_call_name`). Updated tool headers, entry C++ sources, security validators, `src/tools/meson.build`, `meson.build`, system prompt text in `agent_window.cpp`, `docs/tools.md`, and all unit & e2e test files. Verified with 100% test pass (249 OK).
- Tool Parameter Standardization & `apply_text_filter` Input Path (`src/tools/`, `docs/tools.md`, `tests/unit/`): Added `path` parameter support to `apply_text_filter` to accept input directly from workspace or VFS files (`tmp://`). Standardized parameter naming guidelines in `docs/tools.md` to establish the `"path"` standard for file system parameters across all non-image tools, and updated tool validators (`flag_as_error`, `open_in_editor`, `create_code_review_item`, `list_code_review_items`, `run_python`, `agent_get_profile_details`) and unit test cases to strictly use `"path"`. Verified with 100% test pass.
- Multi-Page New Project Creation Wizard (`src/ui/dialog_factories.cpp`, `src/project_template_manager.cpp`, `src/ui/dialog.cpp`): Refactored `create_new_project_dialog()` into a 3-step guided wizard built on `ui_paged_container`. Replaced static dropdowns with responsive `ui_radiobutton_group` controls (Language, Build System, Language Standard) with `"Other"` options for custom projects. Step 1 collects project identity/path with non-empty validation; Step 2 presents language selection (C++, C, Python, Rust, Other); Step 3 dynamically populates matching build systems (Meson, CMake, pyproject.toml, Cargo, None/Custom) and language standards (C++23/20/17, C17/11/99, 3.11+/3.10/3.9, 2021/2018 Edition, Custom/Default). Updated `project_template_manager::create_project` to support custom/other projects without requiring template files. Updated `dialog.cpp` to map wizard finish/next buttons to dialog confirmation. Verified with `unit_project_template_manager`, `unit_test_ui_paged_container`, and full test suite passing (246 OK, 2 SKIPPED).
- Paged Wizard Container Widget `ui_paged_container` (`src/ui/components/ui_paged_container.h/cpp`, `src/ui/ui_element.h`, `src/ui/components/ui_button.h/cpp`, `src/ui/dialog.cpp`): Designed and implemented `ui_paged_container`, a composite container widget for multi-step wizard dialog flows. Only the currently active page and a standardized bottom button bar (`<< Back`, `Cancel`, `Next >>` / `Finish`) are drawn and routed events. Implemented `<< Back` hiding on page 0, label transition to `Finish` on the last page, page validation callback (`on_validate_page`), and page entry callback (`on_page_entered`). Implemented Hybrid Grow-Only Resizing in `flow()` that automatically expands width/height and notifies parent `dialog` when dynamic page content requires more space without shrinking or window flickering. Fixed height oscillation infinite loop between `ui_paged_container::flow()`, page containers (`ui_vertical_flow`), button bars, and `dialog::draw()`. Updated `ui_container` subclass documentation table in `ui_element.h` and added unit test in `tests/unit/test_ui_paged_container.cpp`. Verified with 100% test pass (247 OK, 1 SKIPPED).
- Dynamic AI Model Scoring & Sorting System (`src/agentlib/ai_model.h/cpp`, `src/agentlib/model_server.h/cpp`, `src/agentlib/stale_models.h`, `scripts/add_stale_model.py`): Implemented runtime dynamic model scoring and list sorting (`ai_model::calculate_score()` and `ai_model_registry::get_all_models()`). Scoring combines `model_server` base scores (custom user servers = `0.0`, default vendor servers = `-2.0`, serialized to `servers.json`), major/minor semantic version bonuses (`version / 10.0`), continuous linear age decay based on model creation timestamps, and a `-1.0` penalty for known stale models defined in `src/agentlib/stale_models.h`. Added `scripts/add_stale_model.py` helper CLI to maintain the stale model header. Updated `httplib_transport.cpp` to extract API `created` timestamps and added unit test in `tests/unit/test_ai_model_scoring.cpp`. Verified with 100% test pass (245 OK, 2 SKIPPED).

- Tool Status Window `perf_event_paranoid` Check (`dialog_factories.cpp`): Added `/proc/sys/kernel/perf_event_paranoid` inspection to `create_tool_status_dialog()`. Displays status (`☑ OK (val=1)` vs `☐ Restricted (val=3)`) and provides instructions to run `sudo sysctl -w kernel.perf_event_paranoid=1` when CPU profiling is restricted. Updated unit test in `test_tool_status.cpp`.
- Perf Sampling Fallbacks & Debug Log Forwarding (`perf_catcher.c` & `perf_manager.cpp`): Added `PERF_COUNT_SW_TASK_CLOCK` sampling fallback in `perf_catcher.c` and exact `errno` logging. Updated `perf_manager::parse_and_resolve` to parse and forward `perf_debug_*.txt` files to `event_logger`, providing diagnostic visibility into PMU and software sampling initialization.
- Perf Ring Buffer Size & Debug Log Generator (`perf_catcher.c`): Replaced static 1 MB mmap ring buffer request with adaptive power-of-two page size fallbacks (64 pages -> 16 pages -> 4 pages) to prevent `ENOMEM`/`EPERM` mmap failures when `kernel.perf_event_mlock_kb` is restricted. Added `perf_debug_<pid>.txt` generation in `TURBOSTAR_PERF_DIR` for diagnostic tracking of init and shutdown events.
- Command Runner Sandbox Injection Fix (`command_runner.cpp`): Updated sandbox command builder condition to `(!bypass_sandbox_ && (enable_crash_catcher_ || !perf_dir_.empty()))`, ensuring `LD_PRELOAD` and `TURBOSTAR_PERF_DIR` are always injected into systemd units whenever performance collection is requested.
- Profiling Diagnostic Logging & Multi-Fallback Sampling (`perf_manager.cpp` & `perf_catcher.c`): Added rich `event_logger` logging in `perf_manager::parse_and_resolve` to log target directory scans, file names, file sizes, raw sample counts, and symbol resolution counts. Added multi-tier fallback sampling (`PERF_COUNT_HW_CPU_CYCLES` freq/period -> `PERF_COUNT_SW_CPU_CLOCK` freq/period) in `perf_catcher.c` to guarantee CPU cycle/clock sampling across restricted containers, VMs, and cloud environments. Updated `meson.build`.
- Performance Profile Notifications (`agent_terminate_run` & `agent_wait_for_app`): Enhanced `agent_terminate_run` and `agent_wait_for_app` tool outputs to automatically detect active performance profile data and append `"Performance profile data is available, use agent_get_profile_summary to retrieve this data."`. Updated unit test cases in `test_agent_terminate_run.cpp` and `test_agent_wait_for_app.cpp`.
- Agent Profiling Tools (`agent_get_profile_summary` & `agent_get_profile_details`): Implemented LLM agent tools `agent_get_profile_summary` (returns top N functions and lines by CPU cycle percentage) and `agent_get_profile_details` (returns line-by-line percentage breakdowns for a target source file or function name). Registered tools in `src/tools/meson.build`, updated `docs/tools.md`, and added unit test target in `tests/unit/test_profile_tools.cpp`.
- Agent Performance Sampling & `agent_start_app`: Added optional `collect_performance` boolean parameter to `agent_start_app` tool schema. Wired `collect_performance` through `document_provider`, `editor::start_app`, `editor_event`, `terminal_window`, and `command_runner` to set `TURBOSTAR_PERF_DIR` and grant sandbox permissions for CPU sampling via `libturbocatch.so`. Updated `docs/tools.md` and added unit test case in `tests/unit/test_agent_start_app.cpp`.
- Host `perf_manager` Post-Processor (`src/perf_manager.h/cpp`): Implemented host C++ profiling post-processing pipeline (`turbostar::perf_manager`). Reads raw `perf_samples_<pid>.dat` and `perf_maps_<pid>.txt`, merges partial sample counts, invokes `turbostar::address_lookup::resolve_addresses()`, computes line and function CPU cycle percentages, cleans up temporary raw `/tmp` files, and maintains thread-safe active profile state (`perf_profile_report`). Updated `src/meson.build`, `meson.build`, and added unit test `tests/unit/test_perf_manager.cpp`.
- Pure C `perf_catcher` Preload Module (`src/crash_catcher/perf_catcher.h/c`): Implemented lightweight CPU sampling in `libturbocatch.so` triggered by `TURBOSTAR_PERF_DIR`. Uses `perf_event_open(2)` with hardware PMU cycles and fallback to software CPU clock, zero-thread demand-paged `mmap` ring buffer, static BSS direct-mapped cache (`cache[2048]`), and zero-allocation `write()` system call flushing to write `perf_samples_<pid>.dat` and `/proc/self/maps` to `perf_maps_<pid>.txt`. Updated `meson.build` and added unit test in `tests/unit/test_perf_catcher.cpp`.
## 03-08-2026
- `/rescan` TUI Slash Command & Menu Subagent Hot-Reloading (`src/agentlib/subagent_manager.h/cpp`, `src/agentlib/command_registry.cpp`, `src/ui/menu_bar.cpp`, `src/editor_events.cpp`, `src/editor_events_ui.cpp`, `src/ui/agent_window.cpp`, `tests/unit/test_subagent_manager.cpp`, `docs/todo.md`):
  1. Implemented `/rescan` slash command in `command_registry` and added `Rescan Subagents` menu item under Options menu.
  2. Implemented `subagent_manager::rescan()` to hot-reload custom subagent definitions from disk (`agents/` directory) at runtime without requiring an editor restart.
  3. Added `event_type::rescan_subagents` event routing in central dispatch (`src/editor_events.cpp`) and UI event handler (`src/editor_events_ui.cpp`).
  4. Added `test_subagent_manager_rescan()` unit test suite in `tests/unit/test_subagent_manager.cpp`.
  5. Verified 100% test suite pass rate (247 OK, 1 Expected Fail, 2 Skipped).

## 01-08-2026
- Dynamic `code_review` Non-Default Tool Family Migration (`src/tools/`, `src/agentlib/ai_agent.cpp`, `src/agentlib/tool_validator.cpp`, `src/agentlib/tool_registry.cpp`, `src/codereview_manager.h/cpp`, `src/plugins/securityagent/`, `docs/todo.md`):
  1. Added thread-safe `has_active_items()` helper method to `codereview_manager`.
  2. Implemented dynamic auto-activation for `"code_review"` tool family in `ai_agent::is_tool_family_active()` and `tool_validator::is_allowed_for_agent()` whenever active review items exist.
  3. Assigned `create_code_review_item` and `perform_code_review` to `"base|code_review"` family for default bootstrapping, and assigned `update_code_review_item`, `confirm_code_review_item`, `resolve_code_review_item`, `list_code_review_items`, and `get_code_review_item` to `"code_review"`.
  4. Registered `"code_review"` tool family in `tool_registry` constructor.
  5. Equipped Security Review Agent (`security_review_with_agent`) and Code Reviewer subagents with `"code_review"` tool family.
  6. Verified 100% test suite pass rate (246 OK, 1 Expected Fail, 2 Skipped).
- `editor` Tool Family & Auto-Activation (`src/project_manager.h`, `src/editor.cpp`, `src/agentlib/`, `src/tools/`, `tests/unit/test_editor_tool_family.cpp`, `meson.build`, `docs/todo.md`):
  1. Created `"editor"` tool family for UI-specific tools (`flag_as_error`, `clear_all_errors`, `agent_set_status`, `open_in_editor`). Kept `ask_user` in `"base"` family (always available).
  2. Added `is_editor_mode()` / `set_editor_mode()` tracking to [project_manager.h](file:///home/arjan/git/turbostar2/src/project_manager.h) and set `set_editor_mode(true)` in `editor` constructor.
  3. Added auto-activation logic in `ai_agent::is_tool_family_active()` and `tool_validator::is_allowed_for_agent()` to activate `"editor"` family automatically when in interactive editor UI mode (`is_editor_mode() == true`), while keeping it inactive in headless/server mode.
  4. Registered `"editor"` tool family guidance and reasons in `tool_registry`.
  5. Added unit test [test_editor_tool_family.cpp](file:///home/arjan/git/turbostar2/tests/unit/test_editor_tool_family.cpp) and verified 100% test suite pass rate (247 OK, 1 Expected Fail, 2 Skipped).
  1. Created `src/plugins/sqlite/` containing `plugin.cpp`, `sqlite_create_db.h/cpp/security.cpp`, `sqlite_delete_db.h/cpp/security.cpp`, `sqlite_list_db.h/cpp/security.cpp`, and `sqlite_perform.h/cpp/security.cpp`.
  2. Moved `sqlite` tool family registration and all 4 tools to `src/plugins/sqlite/plugin.cpp` (`plugin_run()` / `plugin_unload()`).
  3. Removed old built-in source directories `src/tools/sqlite_*` and updated `src/tools/meson.build`.
  4. Added `sqlite_plugin` shared module target (`sqlite.so`) in `src/plugins/meson.build` linked with `sqlite3_dep`.
  5. Removed `sqlite3_dep` from core library dependencies in `src/meson.build` (`libturbostar_core`, `libagentlib`, `tools_deps`).
  6. Updated Meson unit test targets (`test_sqlite_list_db` and `test_sqlite_validation`) in `meson.build` with `export_dynamic: true`, `depends: [sqlite_plugin]`, and `env: ['TURBOSTAR_PLUGIN_DIR=' + meson.project_build_root() / 'src' / 'plugins']`.
  7. Fixed singleton initialization order in `test_sqlite_list_db.cpp` and `test_sqlite_validation.cpp` to prevent exit double-free crashes.
  8. Verified 100% test suite pass rate (246 OK, 1 Expected Fail, 2 Skipped).
- `sqlite` Non-Default Tool Family Migration (`src/tools/sqlite_*/`, `src/agentlib/tool_registry.cpp`, `src/agentcli/main.cpp`, `tests/unit/`, `tests/e2e/`, `docs/todo.md`):
  1. Updated `sqlite_create_db_validator`, `sqlite_delete_db_validator`, `sqlite_list_db_validator`, and `sqlite_perform_validator` to return tool family `"sqlite"`.
  2. Registered `sqlite` tool family in `tool_registry` constructor with custom activation reason (*"Activate when inspecting, creating, or querying local SQLite databases, or if you want to keep a todo list in a database"*) and detailed guidance.
  3. Added `--activate-family` CLI option to `agentcli` / `agentcli_replay` and updated `test_sqlite_tools.py` and unit test contexts (`test_sqlite_list_db.cpp`, `test_sqlite_validation.cpp`).
  4. Verified 100% test suite pass rate (246 OK, 1 Expected Fail, 2 Skipped).
- Removal of TODO LLM Tools & Backing Infrastructure (`src/tools/`, `src/agentlib/ai_agent.h/cpp`, `src/ui/agent_window.h/cpp`, `src/system_docs_embedded.h`, `tests/unit/`, `docs/todo.md`):
  1. **Step 1 (Tools & Test Cleanup)**: Removed 6 custom todo tool directories (`agent_add_todo`, `agent_list_todos`, `agent_complete_todo`, `agent_pop_todo`, `agent_delete_todo`, `get_subagent_todo_status`), associated unit/E2E test files, tool alias mappings, and plan mode prompt references.
  2. **Step 2 (Backing Infrastructure & UI Cleanup)**: Removed `todo_item` struct, `todos_` vector, `todos_mutex_`, `save_todos_internal()` persistence, and todo methods (`add_todo`, `get_todos`, `pop_todo`, `mark_todo_complete`, `delete_todo`, `get_todo_reminder_msg`) from `ai_agent.h/cpp`. Cleaned up `agent_window.h/cpp` sidebar rendering and focus cycling to eliminate the TODO listbox panel.
  3. Verified 100% test suite pass rate (246 OK, 1 Expected Fail, 2 Skipped).
- `test_watchdog.h` Assertion Failure Diagnostics (`tests/unit/test_watchdog.h`, `tests/unit/test_assert_fail.cpp`): Enhanced test watchdog assert failure and signal handling. Added `Thread ID` (`get_current_tid()`) and `Elapsed` runtime (`get_elapsed_seconds()`) to `__assert_fail`, `__assert_perror_fail`, `alarm_handler`, and `crash_handler` headers. Verified 100% test suite pass rate (255 OK, 1 Expected Fail, 2 Skipped).
- `system://tool-families.md` & `system://tool-families/<family>.md` Enhancements & Bug Fixes (`src/vfs/system_vfs_provider.cpp`, `src/agentlib/tool_registry.h/cpp`, `src/tools/a2a_connect_server/a2a_connect_server_security.cpp`, `tests/unit/test_system_vfs.cpp`): Resolved test agent evaluation feedback for tool family VFS endpoints:
  1. **Inactive Family Member Tools Catalog**: Updated `tool_registry` with `get_all_registered_validators()` to retrieve all registered tool validators without active family filtering. Updated `generate_tool_family_detail()` to parse single and pipe/comma-delimited family strings (`extract_tool_families()`), successfully populating member tools for inactive families (`hexedit`, `image`, `a2a`, `html`, `binary`, `x86`, etc.) prior to activation.
  2. **Active Status Indicators**: Added `Default Status` column (`**Active**` vs `*Inactive*`) to `system://tool-families.md` overview table and explicit `Activation Status` headers on family detail pages.
  3. **Cross-Referenced Family Aliases**: Dynamically detects tools shared across multiple families (e.g. `binary|hexedit`) and displays clickable `Related / Shared Families` cross-reference links.
  4. **`a2a_connect_server` Family Assignment**: Updated `a2a_connect_server_security.cpp` to return `base|a2a` family membership.
  5. Verified 100% test suite pass rate (255 OK, 1 Expected Fail, 2 Skipped).
- Add `?search=` Query Hint to `tools.md` & `tools_detailed.md` Purpose Descriptions (`src/vfs/system_vfs_provider.cpp`): Added `(supports ?search=<query>)` directly to the `Details` metadata for `tools.md` and `tools_detailed.md` in `system_vfs_provider`. Enables LLMs calling `fs_list_dir("system://", rich_metadata=true)` to immediately discover query-string filtering before reading the files.
- VFS Path Routing for `fs_grep_files` & `fs_file_size` and Dynamic File Metadata Fix (`src/vfs/system_vfs_provider.cpp`, `src/tools/fs_file_size/fs_file_size_entry.cpp`, `src/tools/fs_grep_files/fs_grep_files_entry.cpp`, `tests/unit/test_fs_list_dir.cpp`, `meson.build`):
  1. **Dynamic File Metadata**: Updated `get_file_info()` and `list_directory()` in `system_vfs_provider` to evaluate dynamic generator functions `fn("")`, populating non-zero byte size and line count metadata for top-level dynamic files (`agents.md`, `mcp.md`, `tools.md`, `tools_detailed.md`).
  2. **`fs_file_size` VFS Routing**: Updated `fs_file_size` to query `vfs->get_file_info()` when given virtual scheme paths (e.g. `system://agents.md`), returning accurate byte size without throwing OS `std::filesystem` errors.
  3. **`fs_grep_files` VFS Routing**: Added recursive VFS file search pipeline to `fs_grep_files`, traversing virtual directory trees (e.g. `system://`), reading content via `vfs->read_file()`, and returning formatted RE2 match blocks.
  4. Verified full test suite passing (255 OK, 1 Expected Fail, 2 Skipped).
- `system://` VFS Subdirectory Listing & `fs_list_dir` Directory Support (`src/vfs/system_vfs_provider.cpp`, `src/tools/fs_list_dir/fs_list_dir_entry.cpp`, `tests/unit/test_system_vfs.cpp`, `tests/unit/test_fs_list_dir.cpp`): Fixed a bug where `system_vfs_provider::list_directory()` only returned leaf file URIs without synthesizing subdirectory entries (e.g. `system://languages/` and `system://workflows/`). Updated `list_directory()`, `get_file_info()`, and `exists()` in `system_vfs_provider` to emit directory entries (`type = 'D'`) for intermediate path segments and updated `fs_list_dir` (`scan_vfs()`) to render purpose metadata for directory entries. Added test assertions verifying `system://` root directory listing contains `languages/` and `workflows/` directories. Verified 100% test pass rate (255 OK, 1 Expected Fail, 2 Skipped).
- Simplify Language VFS Purpose Descriptions (`src/vfs/system_vfs_provider.cpp`, `tests/unit/test_system_vfs.cpp`, `tests/unit/test_fs_list_dir.cpp`): Simplified the `languages/` VFS purpose description strings to be direct and concise (e.g. `"Read when writing or refactoring C++23 code."`). Updated unit test assertions in `test_system_vfs.cpp` and `test_fs_list_dir.cpp`. Verified 100% test suite passing (255 OK, 1 Expected Fail, 2 Skipped).
- Custom VFS Purpose Metadata & Rich Directory Listings (`src/agentlib/virtual_file_system.h`, `src/vfs/system_vfs_provider.h/cpp`, `src/tools/fs_list_dir/fs_list_dir_entry.cpp`, `tests/unit/test_system_vfs.cpp`, `tests/unit/test_fs_list_dir.cpp`): Added `std::string details` field to `vfs_file_info` struct and `register_description()` interface in `system_vfs_provider`. Registered "when to read this file" / purpose metadata descriptions for all static language standards (`cpp23.md`, `c17.md`, `python311.md`, `rust2021.md`, `typescript.md`, `verilog.md`), workflows (`code_review.md`, `plan_mode.md`, `crash_analysis.md`), dynamic generators (`agents.md`, `tools.md`, `tools_detailed.md`, `mcp.md`). Updated `fs_list_dir` (`scan_vfs()`) to map `entry.details` into `meta.details` when `rich_metadata=true`. Updated unit tests and verified 100% test pass rate (255 OK, 1 Expected Fail, 2 Skipped).
- `system://tools.md` Pointer Header & Filter Hints (`src/vfs/system_vfs_provider.cpp`, `src/tools/run_python/run_python_entry.cpp`, `tests/unit/test_run_python.cpp`, `tests/unit/test_system_vfs.cpp`): Added explicit Markdown `[!TIP]` header callouts at the top of `system://tools.md` pointing LLMs to `system://tools_detailed.md` for full parameter schema inspection and providing examples of keyword filtering (e.g. `fs_read_lines("system://tools.md?search=git")`). Added `UV_CACHE_DIR=.turbostar/uv_cache` in `run_python_entry.cpp` to prevent read-only filesystem errors when `$HOME` isolation is enabled. Verified full test suite passing (255 OK, 1 Expected Fail, 2 Skipped).
- Deprecate `list_tool_calls` Tool in Favor of `system://` VFS (`src/vfs/system_vfs_provider.h/cpp`, `src/tools/list_tool_calls/`, `docs/tools.md`, `tests/unit/test_system_vfs.cpp`, `meson.build`, `src/tools/meson.build`): Replaced the `list_tool_calls` function-calling tool with `system://tools.md` (summary table with query string `?search=pattern` support) and `system://tools_detailed.md` (and alias `system://tools/details.md` for full parameter schema inspection). Removed `list_tool_calls` tool implementation, schemas, and standalone unit test. Updated `docs/tools.md` and verified full test suite passing (255 OK, 1 Expected Fail, 2 Skipped).
- Template `AGENTS.md` Rule Alignment & Test Isolation (`system/languages/cpp23.md`, `system/languages/c17.md`, `system/languages/python311.md`, `system/languages/rust2021.md`, `tests/unit/test_perform_code_review.cpp`, `tests/unit/test_system_vfs.cpp`): Aligned built-in `system/languages/` rules with conventions from project template `AGENTS.md` files (memory management, string_view/span usage, security considerations, mutex documentation, subclass tables). Updated `test_perform_code_review.cpp` to call `test_watchdog::setup_watchdog(30)` with `$HOME` isolation enabled, preventing parallel test race conditions. Rebuilt embedded header `src/system_docs_embedded.h` and verified full test suite passing (256 OK, 1 Expected Fail, 2 Skipped).
- Built-in Language Guidelines & Workflow System Docs (`system/languages/c17.md`, `system/languages/rust2021.md`, `system/languages/typescript.md`, `system/languages/verilog.md`, `system/workflows/crash_analysis.md`, `src/vfs/system_vfs_provider.cpp`, `src/ui/agent_window.cpp`, `docs/design.md`, `tests/unit/test_system_vfs.cpp`): Added static guidelines for C17, Rust 2021, TypeScript/JavaScript, Verilog, and a generalized crash analysis protocol workflow (`system://workflows/crash_analysis.md`). Expanded fallback URI resolution (`c.md`, `rust.md`, `ts.md`, `verilog.md`, `crash_analysis.md`) and Turn 1 system prompt auto-injection for all wizard-supported languages in `src/ui/agent_window.cpp`. Documented `system://` VFS in `docs/design.md` and verified unit test suite (256 OK, 1 Expected Fail, 2 Skipped).
- `system://` VFS Provider & Demand-Loaded System Prompt Guidelines (`system/`, `scripts/embed_system_docs.py`, `src/system_docs_embedded.h`, `src/vfs/system_vfs_provider.h/cpp`, `src/agentlib/virtual_file_system.cpp`, `src/ui/agent_window.cpp`, `tests/unit/test_system_vfs.cpp`, `meson.build`, `src/meson.build`): Implemented `system://` VFS scheme supporting both static build-time embedded markdown documents (`system/languages/cpp23.md`, `system/languages/python311.md`, `system/workflows/code_review.md`, `system/workflows/plan_mode.md` via `scripts/embed_system_docs.py`) and dynamic runtime generators (`system://agents.md`, `system://tools.md`, `system://mcp.md`). Added root alias resolution (`system://cpp23.md` $\to$ `system://languages/cpp23.md`), layered project/user overrides (`.turbostar/docs/`), and Turn 1 primary language system prompt auto-injection. Added `unit_test_system_vfs`. Verified full test suite passing (256 OK, 1 Expected Fail, 2 Skipped).
- Primary Language & Version Project Preferences (`src/config_manager.h/cpp`, `src/ui/dialog_project.cpp`, `src/ui/dialog_settings.cpp`, `tests/unit/test_primary_language_setting.cpp`, `meson.build`): Added persistent project configuration fields for `primary_language` (e.g. `"C++"`) and `primary_language_version` (e.g. `"C++23"`). Wired settings into New Project Wizard creation logic and the Project Settings tab of the tabbed Preferences dialog. Added unit test `unit_test_primary_language_setting`. Verified full test suite passing (255 OK, 1 Expected Fail, 2 Skipped).
- Test Assertion Hooks & Diagnostics (`tests/unit/test_watchdog.h`, `tests/unit/test_watchdog_assert.cpp`, `meson.build`): Implemented weak C-linkage `__assert_fail` and `__assert_perror_fail` assertion handlers in `test_watchdog.h` with automatic `dlsym(RTLD_NEXT)` forwarding when `TURBOSTAR_DUMP_DIR` is set. When an assertion fails during unit tests, outputs structured diagnostic boxes (file, line number, function, exact `assert(...)` expression string or errno message) and unwinds a demangled C++ call stack via `libunwind`. Added `unit_test_watchdog_assert` in `tests/unit/test_watchdog_assert.cpp` and updated `meson.build`. Verified full test suite passing (254 OK, 1 Expected Fail, 2 Skipped).
- Refactor `src/ui/dialog_factories.cpp` into Domain Files (`src/ui/dialog_basic.cpp`, `src/ui/dialog_settings.cpp`, `src/ui/dialog_ai.cpp`, `src/ui/dialog_a2a.cpp`, `src/ui/dialog_project.cpp`, `src/ui/dialog_factories.h`, `src/ui/dialog.h`, `src/meson.build`): Refactored the 3,244-line `dialog_factories.cpp` into 5 domain-specific source files (Basic/Prompts, Configuration & Settings, AI & Agent & MCP, A2A & Remote, Project & File Dialogs). Preserved top block comment standard and added discoverability header comment in `dialog_factories.cpp`. Reorganized `dialog_factories.h` into domain sections with comments and updated subclass comment table in `dialog.h`. Updated `src/meson.build` to compile all split files into `libturbostar_ui`. Verified full test suite passing (253 OK, 1 Expected Fail, 2 Skipped).

## 28-07-2026
- `hexedit` Tool Family Description Update (`src/plugins/hexedit/plugin.cpp`): Updated the `hexedit` tool family description registered in `plugin_run()` with capability-driven workflow guidance, format discovery details (TAR, ELF, PNG, JPEG, ZIP), and a step-by-step 4-stage in-process binary extraction workflow example (`data_decompress` $\to$ `hexinspect` $\to$ `data_decompress(format='none')` $\to$ `fs_read_lines`). Verified with `unit_test_activate_tool_family` passing.
- `tmp://` Virtual Filesystem Write Access During Plan Mode (`src/tools/`, `src/agentlib/tool_registry.cpp`): Allowed write access to `tmp://` virtual files during Plan Mode across all file writing and editing tools (`fs_write_file`, `fs_replace_content`, `fs_replace_lines`, `fs_mkdir`, and `apply_text_filter`), and updated `tool_registry::prepare_tool` read-only bypass checks. Updated `enter_plan_mode` return message to inform agents that `tmp://` paths remain fully writable for drafting plans. Added unit test assertions to `tests/unit/test_exit_plan_mode.cpp` and `meson.build`. Verified with `unit_test_exit_plan_mode` and full test suite passing (245 OK).
- `fs_replace_content` Staged Relaxed Matching & `function_hint` Scope Resolution (`src/tools/fs_replace_content/`): Implemented a 3-stage matching pipeline for LLM code replacements: (1) Strict exact matching, (2) LSP symbol / regex scope resolution using the new optional `function_hint` parameter, and (3) Staged relaxed matching (Level A: CRLF/trailing space normalization, Level B: Tab-space equivalence, Level C: Leading whitespace normalization for multi-line blocks $\ge 3$ lines). Added a 4-combo hint state error matrix (`format_multiple_matches_error`) guiding the LLM to supply `function_hint` first, `line_hint` second, or expand `target_content`. Added unit tests 8 through 11 in `tests/unit/test_fs_replace_content.cpp`. Verified with `unit_test_fs_replace_content` passing.

## 27-07-2026
- Refactor `document.h` / `document.cpp` & Fix Concurrency Data Races (`src/document.h`, `src/document.cpp`, `src/document_edit.cpp`, `src/document_selection.cpp`, `tests/unit/test_document.cpp`): Fixed thread-safety data races in inline getters `get_lsp_highlights()`, `get_lsp_diagnostics()`, and `get_enclosing_scope()` by acquiring `std::shared_lock(mutex_)` and returning by value instead of un-locked const references. Refactored non-virtual method parameters to accept `std::string_view` (`insert_file`, `insert_char`, `insert_text`, `append_line`, `set_git_branch`, `write_selection_to_file`, `apply_external_edits_json`) to eliminate temporary string allocations. Added `noexcept` annotations (`is_read_only`, `has_selection`, `has_nondefault_filename`, `get_ignore_disk_changes`). Added multi-threaded LSP diagnostic and highlight thread-safety unit test to `test_document.cpp`.
- Refactor `line.h` / `line.cpp` and Fix Threading & Deadlock Bugs (`src/line.h`, `src/line.cpp`, `tests/unit/test_line.cpp`): Fixed critical thread-safety bug in `display_col_to_char_pos()` where `shared_lock(mutex_)` was recursively re-acquired via `char_to_display_col()` on non-recursive `std::shared_mutex` (causing UB/deadlock). Introduced `char_to_display_col_unlocked()`. Added self-merge guard (`if (this == &other_line) return;`) to `line::merge` to prevent deadlocks when merging a line with itself. Updated constructors and methods to accept `std::string_view` (`explicit line(std::string_view)`, `set_text(std::string_view)`, `insert_at(...)`) to eliminate temporary string allocations. Added `noexcept` annotations (`line() noexcept = default;`, `byte_at_unlocked(...) const noexcept`). Added self-merge and concurrent multi-threaded deadlock unit test cases to `test_line.cpp`.
- Synchronize Coding Rules from `AGENTS.md` to `GEMINI.md` (`GEMINI.md`): Integrated core guidelines from `templates/meson_cpp/AGENTS.md` into `GEMINI.md`, including strict RAII (no raw `new`/`delete`), `std::string`/`std::string_view` preference over raw `char *`, Standard Library container/algorithm priority, `constexpr`/`const`/`noexcept` annotations, security-first input validation & labeling, and rationale-focused commenting ("why" over "what").

## 26-07-2026
- New Project Creation Wizard & Template Engine (`templates/`, `scripts/embed_templates.py`, `project_template_manager.h/cpp`, `dialog_factories.h/cpp`, `menu_bar.cpp`, `editor.cpp`, `editor_events_key.cpp`, `editor_events_ui.cpp`, `test_project_template_manager.cpp`): Added embedded project template engine supporting Meson C++, Meson C, CMake C++, CMake C, Python (`pyproject.toml`), and Rust (`Cargo`). Created `embed_templates.py` Python build step compiling template files into static C++ byte arrays in `project_templates_embedded.h`. Implemented `project_template_manager` with `@@VAR@@` token replacements (`@@PROJECT_NAME@@`, `@@EXECUTABLE_NAME@@`, `@@AUTHOR_NAME@@`, `@@AUTHOR_EMAIL@@`, `@@YEAR@@`, `@@LANGUAGE_STD@@`), `.C++17` version override fallback selection, and automatic `git init` + initial commit. Designed reactive TUI `create_new_project_dialog` with cascading `ui_dropdown` candidate updates when Language changes. Synchronized user's build system choice directly to `config_manager` (`meson`, `cmake`, `cargo`, `python`) upon project creation. Added `File -> New Project...` menu item and empty-directory auto-trigger on startup. Added unit test in `test_project_template_manager.cpp`.
- Image Provenance & Origin Chain Tracking in `images://` VFS Namespace (`image_manager.h/cpp`, `image_rotate_tool.cpp`, `image_resize_tool.cpp`, `image_crop_tool.cpp`, `image_mirror_tool.cpp`, `image_grayscale_tool.cpp`, `image_threshold_tool.cpp`, `image_compose_tool.cpp`, `test_image_tools.cpp`): Added `origin_file` (canonical SHA-256 hash) and `origin_ops` metadata fields to `image_metadata`, persisted in `mappings.json`. Added `get_canonical_sha256()` helper to resolve VFS aliases/URIs to canonical parent hashes, updated `ingest_image()` to store provenance data, and implemented `get_origin_chain()` and `format_origin_chain()` to reconstruct the transformation history (e.g., `images://logo.jpg -> rotate(90) -> crop(5,5,20,20)`). Updated all `image_basic` plugin tools to pass provenance metadata upon output ingestion. Added unit test suite 15 in `test_image_tools.cpp`.