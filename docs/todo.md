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

- cleanup: the long list of compat names for tool calls in src/agentlib/llm_types.h should become a std::map<>
- our test case crash catcher should hook into the assert logging logic as well and more clearly print assert errors?

- test case linking -- we have may test cases (good!) but linking them takes a lot of time, we may be "overlinking" stuff into them
	- we need a better strategy

- language specific system prompt feature
	- detect the language that the project uses -- or have our welcome dialog write it to the project level config file?
	- allow for language (and maybe language version?) specific system prompt text to be inserted

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

- feature: subagent creation wizzard dialog

- feature: model aliases -- have a set of aliases we can map to actual models, and we can have our "Task Models" UI to select models 
   for specific tasks set up these aliases.
	- base
	- plan
	- review
	- code
	- image



- feature: add a /rescan TUI slash command/shortcut to hot-reload custom subagents inside subagent_manager during runtime.

- feature: become an A2A server for our agents
	- **A2A Agent Card Synthesis & Sidecar Storage**: Hybrid 3-tier resolution policy (Local sidecar `<agent_name>.card.json` -> Global cache `~/.cache/turbostar/agent_cards/<hash>.json` -> LLM Synthesis).
	- **Dedicated `:plugin:a2a` Tool Family**: Keep global namespace clean by grouping A2A tools (`a2a_validate_card`, `a2a_synthesize_card`, `a2a_publish_card`) under the `a2a` family.
	- **`a2a_validate_card` Tool**: Validates synthesized or user-edited `.card.json` files against the formal A2A Agent Card JSON Schema specification.

- feature: become a client to A2A
	we need to parse agent cards and hook them up to our subagent directory

- feature: build an A2A directory
	- maybe a .local style directory service? is there a protocol for autodiscovery?

- feature: have a turboserver mode where we are headless but only serve A2A requests?

- feature:a /command action that activates a tool family (via menu?)

- feature: separate model name database of famous models for default properties
  should investigate the compile time json-to-struct stuff
	- https://raw.githubusercontent.com/BerriAI/litellm/refs/heads/litellm_internal_staging/model_prices_and_context_window.json

- feature: separate model provider (server) database for easy population of model servers
	- need a smart combo box or radio box for "from database" vs "custom" so that it is easy to add either

- feature: have a separate model option for "plan mode" phase

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

## 30-07-2026
- Subagent Orchestration Tool Suite Renaming (`src/tools/`, `src/agentlib/llm_types.h`, `docs/tools.md`, `tests/unit/`, `tests/e2e/`): Renamed subagent orchestration tools to match industry-standard naming conventions: `create_agent` $\to$ `invoke_subagent` (with `local_only` boolean parameter), `message_agent` $\to$ `send_message`, `list_agents` $\to$ `list_subagents`, `agent_status` $\to$ `get_subagent_status`, `wait_for_agent` $\to$ `wait_for_subagent`, `agent_get_output` $\to$ `get_subagent_output`, `agent_report_final_result` $\to$ `report_final_result`, `end_agent` $\to$ `kill_subagent`, and `agent_todo_status` $\to$ `get_subagent_todo_status`. Added compatibility alias resolution in `src/agentlib/llm_types.h` (`normalize_tool_call_name`). Updated tool headers, entry C++ sources, security validators, `src/tools/meson.build`, `meson.build`, system prompt text in `agent_window.cpp`, `docs/tools.md`, and all unit & e2e test files. Verified with 100% test pass (249 OK).

## 29-07-2026
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
- Standalone `address_lookup` Component (`src/address_lookup.h/cpp`): Created a centralized, standalone address translation module (`turbostar::address_lookup`) to convert raw memory addresses into function names, file paths, and line numbers. Implemented high-performance batch address deduplication, secure absolute binary checks (`/usr/bin/eu-addr2line` and `/usr/bin/addr2line`), and maps parsing (`parse_maps`). Refactored `src/crash_process.cpp` and `src/crashdump_manager.cpp` to use `address_lookup`, updated build files (`src/meson.build`, `meson.build`), and added unit tests in `tests/unit/test_address_lookup.cpp`.

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