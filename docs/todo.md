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

- test case linking -- we have may test cases (good!) but linking them takes a lot of time, we may be "overlinking" stuff into them
	- we need a better strategy

- language specific system prompt feature
	- detect the language that the project uses -- or have our welcome dialog write it to the project level config file?
	- allow for language (and maybe language version?) specific system prompt text to be inserted

- allow tmp:// write access during plan mode

- agent connection keepalive -- if the request takes a long time, is there a way to do a keepalive to keep the connection from dropping

- 1. **Highlight Differences (Hex Diffing)**:
   * *The Idea*: Add a companion tool `hexdiff` that takes two file paths, compares them, and highlights only the changed bytes along with
	their structural roles. This would be incredibly useful for verifying binary patches or analyzing compiler optimization impacts.
	- doing this right is VERY complicated, we would need the bsdiff algorithm and then expand the result in an agent
	  friendly format

- 3. **Interactive Byte Editor Interface**:
	   * *The Idea*: A high-level visual representation of the hex edits in the editor UI (similar to how `flag_as_error` creates an overlay)
	would help developers track what modifications the agent is proposing in binary formats.

- fs_replace_content improvement: tabs vs spaces seems to confuse the agent

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


## 27-07-2026
- Refactor `document.h` / `document.cpp` & Fix Concurrency Data Races (`src/document.h`, `src/document.cpp`, `src/document_edit.cpp`, `src/document_selection.cpp`, `tests/unit/test_document.cpp`): Fixed thread-safety data races in inline getters `get_lsp_highlights()`, `get_lsp_diagnostics()`, and `get_enclosing_scope()` by acquiring `std::shared_lock(mutex_)` and returning by value instead of un-locked const references. Refactored non-virtual method parameters to accept `std::string_view` (`insert_file`, `insert_char`, `insert_text`, `append_line`, `set_git_branch`, `write_selection_to_file`, `apply_external_edits_json`) to eliminate temporary string allocations. Added `noexcept` annotations (`is_read_only`, `has_selection`, `has_nondefault_filename`, `get_ignore_disk_changes`). Added multi-threaded LSP diagnostic and highlight thread-safety unit test to `test_document.cpp`.
- Refactor `line.h` / `line.cpp` and Fix Threading & Deadlock Bugs (`src/line.h`, `src/line.cpp`, `tests/unit/test_line.cpp`): Fixed critical thread-safety bug in `display_col_to_char_pos()` where `shared_lock(mutex_)` was recursively re-acquired via `char_to_display_col()` on non-recursive `std::shared_mutex` (causing UB/deadlock). Introduced `char_to_display_col_unlocked()`. Added self-merge guard (`if (this == &other_line) return;`) to `line::merge` to prevent deadlocks when merging a line with itself. Updated constructors and methods to accept `std::string_view` (`explicit line(std::string_view)`, `set_text(std::string_view)`, `insert_at(...)`) to eliminate temporary string allocations. Added `noexcept` annotations (`line() noexcept = default;`, `byte_at_unlocked(...) const noexcept`). Added self-merge and concurrent multi-threaded deadlock unit test cases to `test_line.cpp`.
- Synchronize Coding Rules from `AGENTS.md` to `GEMINI.md` (`GEMINI.md`): Integrated core guidelines from `templates/meson_cpp/AGENTS.md` into `GEMINI.md`, including strict RAII (no raw `new`/`delete`), `std::string`/`std::string_view` preference over raw `char *`, Standard Library container/algorithm priority, `constexpr`/`const`/`noexcept` annotations, security-first input validation & labeling, and rationale-focused commenting ("why" over "what").

## 26-07-2026
- New Project Creation Wizard & Template Engine (`templates/`, `scripts/embed_templates.py`, `project_template_manager.h/cpp`, `dialog_factories.h/cpp`, `menu_bar.cpp`, `editor.cpp`, `editor_events_key.cpp`, `editor_events_ui.cpp`, `test_project_template_manager.cpp`): Added embedded project template engine supporting Meson C++, Meson C, CMake C++, CMake C, Python (`pyproject.toml`), and Rust (`Cargo`). Created `embed_templates.py` Python build step compiling template files into static C++ byte arrays in `project_templates_embedded.h`. Implemented `project_template_manager` with `@@VAR@@` token replacements (`@@PROJECT_NAME@@`, `@@EXECUTABLE_NAME@@`, `@@AUTHOR_NAME@@`, `@@AUTHOR_EMAIL@@`, `@@YEAR@@`, `@@LANGUAGE_STD@@`), `.C++17` version override fallback selection, and automatic `git init` + initial commit. Designed reactive TUI `create_new_project_dialog` with cascading `ui_dropdown` candidate updates when Language changes. Synchronized user's build system choice directly to `config_manager` (`meson`, `cmake`, `cargo`, `python`) upon project creation. Added `File -> New Project...` menu item and empty-directory auto-trigger on startup. Added unit test in `test_project_template_manager.cpp`.
- Image Provenance & Origin Chain Tracking in `images://` VFS Namespace (`image_manager.h/cpp`, `image_rotate_tool.cpp`, `image_resize_tool.cpp`, `image_crop_tool.cpp`, `image_mirror_tool.cpp`, `image_grayscale_tool.cpp`, `image_threshold_tool.cpp`, `image_compose_tool.cpp`, `test_image_tools.cpp`): Added `origin_file` (canonical SHA-256 hash) and `origin_ops` metadata fields to `image_metadata`, persisted in `mappings.json`. Added `get_canonical_sha256()` helper to resolve VFS aliases/URIs to canonical parent hashes, updated `ingest_image()` to store provenance data, and implemented `get_origin_chain()` and `format_origin_chain()` to reconstruct the transformation history (e.g., `images://logo.jpg -> rotate(90) -> crop(5,5,20,20)`). Updated all `image_basic` plugin tools to pass provenance metadata upon output ingestion. Added unit test suite 15 in `test_image_tools.cpp`.

## 25-07-2026
- Lock-Free Signal-Safe Debug Log Ring Buffer for Crash Reports (`event_logger.h/cpp`, `crash_handler.cpp`, `test_event_logger.cpp`): Implemented a BSS-allocated circular ring buffer (`LOG_RING_SLOTS = 16`, `LOG_RING_SLOT_SIZE = 256`) with a two-phase atomic length-publishing protocol (`length = 0` during writes, published post-copy). Updated `event_logger::log()` to ensure trailing `\n` is handled on the logging thread. Added `event_logger::dump_recent_logs_signal_safe()` and integrated it into `crash_handler` signal and exception handlers to append recent debug log lines to crash files and stderr without malloc, mutexes, or `strlen()` calls. Added unit test in `test_event_logger.cpp`.
- Git Commit Hash in Crash Handler Reports (`meson.build`, `src/meson.build`, `src/git_version.h.in`, `src/crash_handler.cpp`): Configured `vcs_tag()` in Meson with `git describe --always --dirty` to generate `git_version.h` (`TURBOSTAR_GIT_HASH`). Updated `crash_handler::fallback_signal_handler` and `fallback_terminate_handler` to write `Git Commit: <hash>` to stderr and crash dump files with 100% signal safety.
- Streamlined Crash Analysis Protocol & Crash Log Reference (`docs/turbostar-crash-analysis-protocol.md`, `src/crash_handler.cpp`): Refined the 3-step **What – How – Where** protocol documentation into concise imperative guidelines for AI agents, adding explicit testcase creation requirement at the end of the **How** step to verify hypotheses prior to fixing and prove resolution afterwards. Added `Analysis Protocol: See docs/turbostar-crash-analysis-protocol.md` reference to crash report outputs in `crash_handler`.

## 24-07-2026
- Technical Details Webpage & Navigation Link (`docs/details.html`, `docs/index.html`, `docs/editor.html`, `docs/ai.html`, `meson.build`): Added new `docs/details.html` page styled consistently with the website design system, featuring a Table of Contents and technical feature deep-dive section skeletons. Updated top header navigation and footer links across all pages, and added `website_validate_details` to the `meson.build` website test suite.

## 23-07-2026
- Multi-Run Performance Profile Storage & Comparison via `run_id` (`perf_manager.h/cpp`, `command_runner.h/cpp`, `terminal_window.cpp`, `agent_get_profile_summary.h/cpp`, `agent_get_profile_details.h/cpp`, `docs/design-perf-integration.md`, `test_perf_manager.cpp`, `test_profile_tools.cpp`): Added in-memory report storage (`saved_reports_[run_id]`) and string ID normalization in `perf_manager`. Updated `command_runner` and `terminal_window` to associate execution session IDs (`"run_N"`) with profiled output runs. Updated `agent_get_profile_summary` and `agent_get_profile_details` JSON schemas and handlers to accept an optional `run_id` parameter, enabling LLM agents to compare performance cycle counts across multiple execution runs.

## 22-07-2026
- Menu Navigation Disabled Items Fix & Category Selection Helper (`menu_bar.h/cpp`): Added `select_category(index)` private helper method and updated `find_next_item()` and `find_prev_item()` to skip disabled items (`is_disabled`) alongside separators (`is_separator`). Prevents vertical arrow navigation (`KEY_UP`/`KEY_DOWN`) and horizontal category switching (`KEY_LEFT`/`KEY_RIGHT`/Alt keys/Mouse clicks) from landing on disabled menu options.
- `document::set_cursor_position` API & `move_cursor` Evaluation Order Fix (`document.h`, `document_nav.cpp`, `editor_events_ui.cpp`): Added atomic `document::set_cursor_position(col, line)` method for direct absolute cursor placement. Re-ordered `move_cursor(dx, dy)` in `document_nav.cpp` to evaluate vertical movement (`dy`) before horizontal (`dx`), preventing single-character right-arrow line wrapping from corrupting vertical line deltas.
- "Go to Next Hotspot" (`F7`) Navigation (`perf_manager.h/cpp`, `address_lookup.h/cpp`, `menu_bar.cpp`, `editor.cpp`, `editor_events.cpp`, `editor_events_key.cpp`, `editor_events_ui.cpp`, `docs/design-perf-integration.md`, `docs/keybindings.md`): Added `go_to_hotspot_possible()` and `get_next_hotspot()` to `perf_manager`. Added `column_number` tracking to `address_lookup` and `perf_line_sample` (with separate line/column `try/catch` parsing). Added `"Go to next hotspot"` to the **Run** menu (grayed out when no profile exists), bound to **`F7`** (`event_type::go_to_next_hotspot`). Automatically focuses existing editor windows or opens unopened files, moving the cursor to line and column offsets and centering the view. Updated `keybindings.md` table to document `F1`–`F10` function keys.
- Concise Performance Status Line Message (`perf_manager.cpp`, `test_perf_manager.cpp`, `docs/design-perf-integration.md`): Simplified line profile status string to `Perf: X% (N samples)` (e.g. `Perf: 4.5% (230 samples)`), removing redundant function name signatures to save status bar space when multiple messages stack.
- Dithered Density Blocks Left Border Heatmap (`window.cpp`, `docs/design-perf-integration.md`): Updated editor left border rendering to scale character density alongside color pairs: Light Shade `░` in Bright Cyan ($\ge 1\%$), Medium Shade `▒` in Bright Yellow ($\ge 10\%$), and Solid Block `█` in Bright Red ($\ge 50\%$).
- Early Memory Map Dump & `perf_maps` PID Pairing (`perf_catcher.c` & `perf_manager.cpp`): Added early `/proc/self/maps` dumping in `perf_catcher_init` so `perf_maps_<pid>.txt` is created immediately when the child process starts. Fixed `perf_manager::parse_and_resolve` to match `maps_path` specifically against `samples_path` PID, avoiding ASLR base address mismatches from stale maps files of previous runs.
- `perf_manager` Map Key Path Normalization (`perf_manager.cpp`): Updated `parse_and_resolve` to process `norm_file_path` through `fs_utils::make_relative_to_project(norm_file_path)`, ensuring `line_samples_by_file` map keys match relative project paths (e.g. `src/main.cpp`). Added fallback map lookups for raw filename parameters in `get_line_profile_percentage` and `get_line_profile_statusmsg`.
- Centralized `command_runner` Child Exit Artifact Processing (`command_runner.h/cpp`, `terminal_window.h/cpp`): Added `command_runner::on_child_exit()` to handle both crashdump refreshing and performance profile parsing (`perf_manager::parse_and_resolve`) upon process termination. Integrated into `command_runner::execute()` and `terminal_window` (`update_pty()` / `stop_process()`), guaranteeing automatic profile resolution and editor visualization regardless of whether execution is synchronous or interactive PTY.
- Editor Window Left Border Perf Heatmap & Status Bar Message (`window.cpp` & `editor.cpp`): Updated `window::draw_border()` to compute document line numbers `(top_line_ + 1 + y)` for visible text rows and render Heavy Vertical Bar (`┃`) on the left border frame in White ($\ge 1\%$), Yellow ($\ge 10\%$), or Red ($\ge 50\%$). Updated `editor::draw()` to automatically set status bar `status_priorities::HOVER` messages when the cursor moves onto lines with performance samples.
- Run (Performance Profile) Menu Option (`menu_bar.cpp`, `event_queue.h`, `editor.cpp`, `editor_events.cpp`, `editor_events_ui.cpp`): Added `event_type::run_profile` enum value, added `"Run (Performance profile)"` as the 3rd item under the Run menu, mapped central dispatch routing in `editor::dispatch`, and implemented event handler in `editor_events_ui.cpp` calling `start_app(args, false, true, true)` to trigger profiling execution.
- Editor UI Query Interface & Document Listener on `perf_manager` (`perf_manager.h/cpp`, `document.h`): Implemented `get_line_profile_percentage()`, `get_line_profile_statusmsg()`, `is_file_profile_valid()`, `invalidate_file()`, and `document_listener` callbacks (`on_line_inserted`, `on_line_deleted`) on `perf_manager`. Allows the editor drawing pipeline to query line percentages and status bar strings, and dynamically shifts sample line numbers upon line insertions/deletions until $\ge 20$ edits invalidate the snapshot.
- `std::ostringstream` to `std::format` Refactor (`event_logger.cpp`, `crashdump_manager.cpp`, `agent_status_entry.cpp`, `agent_list_todos_entry.cpp`, `agent_todo_status_entry.cpp`, `sqlite_list_db_entry.cpp`, `agent_list_episodes_entry.cpp`): Replaced stream-based `ostringstream` boilerplate with C++20/C++23 `std::format` across logging, crashdump tables, and agent status tools.
- Centralized Project-Relative Path Formatting (`fs_utils.h/cpp`, `agent_get_profile_summary_entry.cpp` & `agent_get_profile_details_entry.cpp`): Added `fs_utils::make_relative_to_project` helper to strip project root prefixes consistently across all profiling tools and summary reports.
- Profile Details File Percentage Field Naming (`agent_get_profile_details_entry.cpp` & `docs/tools.md`): Updated `agent_get_profile_details` to return `file_percentage` instead of `function_percentage` when line samples are filtered by `file_path`. Eliminates ambiguity for LLM agents when evaluating file-scope vs function-scope CPU cycle percentages.
- LSP Symbol Range Tightening & Trailing Comment Trimming (`agent_get_profile_details_entry.cpp`): Implemented `tighten_symbol_bounds` to inspect source code lines backwards from LSP `end_line` and strip trailing comment-only or blank lines. Prevents function profile detail context snippets from bleeding into neighboring function headers or comments.
- Parameter-Fuzzy Function Symbol Matching (`agent_get_profile_details_entry.cpp`): Implemented `match_symbol_string` to strip parameter lists `(...)` from query strings and candidate demangled symbols if exact substring matching fails. Allows `agent_get_profile_details` queries like `is_prime_vC()` to successfully match demangled C++ signatures like `is_prime_vC(int, double)`. Updated unit test in `test_profile_tools.cpp`.
- Address-Anchored `addr2line` Output Parsing (`address_lookup.cpp`): Added `-a` (print address) flag to `eu-addr2line` and `addr2line` invocations and introduced `parse_addr2line_output()`. Anchors each result block by its hex address header (`0x...`), preventing indexing drift when `addr2line` outputs extra inlined outer frames or discriminator comments. Fixes bug where all top functions erroneously collapsed to line 14.
- Hottest Line & Function Association Accuracy (`perf_manager.cpp`): Updated `perf_manager` sample aggregation: top function reports now select the most frequent (hottest) line number within each function (`f.line_counts`) rather than taking the arbitrary line number of the first sample encountered in memory order. Similarly, top line samples select the most frequent function name (`l.func_counts`). Fixes line/function mismatches where `is_prime_vA` reported an empty line outside the function.
- Main Interactive Agent Lifecycle Fix & Status Transition Logging (`ai_agent.cpp`): Fixed bug where main interactive session agents transitioned to the terminal `agent_status::dead` state upon completing a turn if `final_result` was recorded. Scoped `dead` transitions strictly to subagents (`is_exit_implicitly_on_idle() || parent != nullptr`), ensuring main agents clear transient final results and return to `agent_status::idle` to accept subsequent user prompts. Added rich diagnostic logging to `ai_agent::set_status()` (`"Agent 1 status: thinking -> idle (parent_id=-1)"`).
- Post-Processing Shutdown Diagnostic Logging (`perf_catcher.c`): Updated `turboperf_shutdown` in `libturbocatch.so` to append detailed post-processing diagnostics to `perf_debug_<pid>.txt`: ring buffer `head` and `tail` offsets, count of raw `PERF_RECORD_SAMPLE` records read, BSS cache slots flushed, total aggregated samples written to `.dat`, and maps file byte size.
- Robust Multi-Candidate Source Path Resolution (`agent_get_profile_details_entry.cpp` & `perf_manager.cpp`): Updated `resolve_file_path` and `perf_manager` to strip trailing line numbers (`:17`) from `file_path` strings before file existence checks and normalize paths using `lexically_normal()`. Added `make_relative_to_project()` in `agent_get_profile_details` to subtract the project root path so `/home/arjan/git/turbotest/main.cpp` displays concisely as `"main.cpp"`.
- LSP Function Bounds Query & Scope Clamping (`agent_get_profile_details_entry.cpp`): Integrated synchronous `lsp_query_document_symbols` into `agent_get_profile_details`. Retrieves exact function start (`func_start`) and end (`func_end`) line boundaries from LSP, clamps ±2 line context strictly inside function boundaries, and guarantees that the function signature header and closing brace are always rendered. Falls back seamlessly to file-relative bounds when LSP is unavailable.
- Profile Details Output Formatting & Context Merging (`agent_get_profile_details_entry.cpp`): Redesigned `agent_get_profile_details` tool output: fetches source code lines (`code`), lifts `file_path` to top-level JSON header, sorts entries by `line_number` ascending, expands and merges ±2 context lines around hot spots into continuous blocks, retains raw `count` samples, and provides both `global_percentage` and `function_percentage`. Updated `docs/tools.md` and unit test `test_profile_tools.cpp`.
- Agent Profile Details Function Matching Fix (`agent_get_profile_details_entry.cpp` & `perf_manager.cpp`): Added `function_name` field directly to `perf_line_sample` struct and replaced rigid exact string equality (`==`) with case-insensitive substring matching (`match_string`) in `agent_get_profile_details`. Allows querying functions without demangled parameter signatures (e.g. `is_prime_vB` matching `is_prime_vB(int)`). Updated unit test `test_profile_tools.cpp`.
- Perf Sampling Rate Tuning & `TURBOSTAR_PERF_FREQ` Support (`perf_catcher.c`): Increased default sampling frequency from 1,000 Hz to 4,000 Hz (4 kHz) and period fallback from 100,000 to 25,000 cycles for higher sample density during short CPU bursts. Added support for `TURBOSTAR_PERF_FREQ` environment variable to allow customizing sampling rates up to 100,000 Hz.
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

## 27-07-2026
- UTF-8 Fast-Path & `std::string_view` Optimization (`src/utf8.h/cpp`): Added an ASCII fast-path to `utf8::display_width()` to bypass `mbrtowc`/`wcwidth` for standard ASCII text, accelerating TUI rendering. Inlined `char_len` as `constexpr noexcept` in `src/utf8.h`, updated `wrap_string` parameters to `std::string_view`, and added `noexcept` annotations across string query routines. Updated `tests/unit/test_utf8.cpp` with ASCII fast-path tests. Verified with 100% test suite pass (245/245 tests ok).
- Event Logger `std::string_view` & Formatting Optimization (`src/event_logger.h/cpp`): Refactored `event_logger` methods (`set_log_file`, `log`, `get_latest_matching_message`) to take `std::string_view`. Optimized `event_logger::log()` to execute `std::format` once per invocation instead of twice, eliminating 50% of timestamp formatting overhead. Updated `tests/unit/test_event_logger.cpp` with `std::string_view` test cases. All tests pass (100% OK).
- Editor & Document Provider `std::string_view` & `noexcept` Modernization (`document_provider.h`, `editor.h`, `editor.cpp`, `editor_events_*.cpp`): Refactored `document_provider` and `editor` method parameter signatures (`set_focus`, `set_status_message`, `start_app`, `start_coredump_gdb`, `write_to_run`, `wait_for_app`, `get_open_document`, `apply_live_edits`, `open_file_as_text`, `open_file_as_binary`, `launch_inline_agent`, `execute_vim_command`) to accept `std::string_view` and added `noexcept` annotations (`is_main_thread`, `get_latency_spikes_for_testing`). Replaced string concatenations with `std::format`. Updated mock document providers across all unit tests. Verified with 100% test suite pass (245/245 tests ok).
- Document Thread Safety & API Modernization (`src/document.h/cpp`): Fixed thread-safety data race in inline getters (`get_lsp_highlights()`, `get_lsp_diagnostics()`, `get_enclosing_scope()`) by acquiring `std::shared_lock(mutex_)` and returning by value. Updated non-virtual methods to `std::string_view` and added `noexcept` annotations.
- Line Thread Safety & API Modernization (`src/line.h/cpp`): Fixed self-merge deadlock and non-recursive `std::shared_mutex` deadlock using `char_to_display_col_unlocked()`. Added self-merge guard, updated constructors and mutators to `std::string_view`, and added `noexcept` annotations.
- GEMINI.md & AGENTS.md Guidelines Synchronization: Updated `GEMINI.md` and `templates/meson_cpp/AGENTS.md` to mandate preferring `std::string_view` for read-only string function parameters over `const std::string &` to avoid unnecessary allocations, alongside RAII, C++23, STL containers/algorithms, security input validation, and rationale comments.
- Centralized Build System Sanitization (`src/config_manager.cpp`): Refactored `config_manager::set_build_system()` to normalize build system inputs (`pyproject.toml` -> `python`, lowercasing) and simplified `apply_new_project_from_dialog()`.
- Application Crash Notifications & `agent_wait_for_app`: Added crash cookie tracking (`TURBOSTAR_CRASH_COOKIE=run_<id>`) to `crash_catcher.c`, `command_runner`, and `terminal_window`. Implemented `agent_wait_for_app` tool to allow agents to wait for process termination, crash, or settled state (500ms output silence). Enhanced `agent_start_app` (with `wait_for_time`) and `agent_terminate_run` to automatically detect application crashes and return detailed `crash_notification` text to guide the LLM agent toward investigation. Added unit tests in `test_agent_wait_for_app.cpp` and updated `docs/tools.md`.
- `agent_get_run_screenshot` Crash & Status Awareness: Upgraded `agent_get_run_screenshot` and `run_screenshot_data` to include `is_alive` (indicating if the target application process is still running or ended) and `crash_notification` (containing formatted crash dumps and debugging guidance if a crash occurred). Updated unit tests in `test_agent_get_run_screenshot.cpp` and updated `docs/tools.md`.
- `crashdump_list` Cookie Column: Extended `crashdump_manager` (`to_markdown_row` and `get_markdown_table`) to display `Cookie` in crash dump tables (`| Crash ID | Timestamp | Executable | Signal | Cookie |`), linking crash dumps directly to `run_<id>` process execution handles. Updated unit tests in `test_crashdump_list.cpp`.
- `crashdump_list` Limit Parameter: Added a `limit` integer parameter (defaulting to 20, returning the most recent ones) to `crashdump_list` to prevent context window bloat as crash history accumulates over time. Updated `crashdump_manager::get_markdown_table` and tests.
- `statistics_manager` Auto-load & Agent Tool Call Tracking Fix: Fixed a bug where `statistics.json` in `~/.cache/turbostar/` was not recording tool call usage during agent runs because `ai_agent::start_processing` invoked tool execution directly without notifying `statistics_manager`. Implemented lazy auto-loading in `statistics_manager` (`increment_stat`, `get_stat`, `get_all_stats`) to ensure existing statistics are loaded before any writes to prevent data loss. Updated `ai_agent.cpp` and `test_statistics_manager.cpp`.
- AI Web Page GDB Core Dump Section: Added a dedicated feature section in `docs/ai.html` (`#crash-debugging`) showcasing automated crash notifications, crash cookie tracking (`run_<id>`), and autonomous GDB core dump inspection with `docs/gdb.svg`. Checked HTML validation via `website_validate_ai`.
- `/clear` Agent Window Command: Implemented `/clear` slash command and `ai_agent::clear_conversation()` to clear conversation history, reset active tokens/compaction state, terminate active subagents, and re-initialize the baseline system prompt while preserving active model configuration, tool families, and workspace bindings. Added UI status hint updates in `agent_window.cpp` and unit tests in `test_clear_command.cpp`.
- Performance Profiling Architecture Document: Authored `docs/design-perf-integration.md` detailing the performance profiling subsystem architecture using `perf_event_open(2)` via `libturbocatch.so`, in-memory IP histogramming, batch symbol resolution using persistent pipes to `eu-addr2line`, and editor UI/AI agent presentation models. Updated `GEMINI.md`.




## 20-07-2026
- Interactive Instructions in agent_debug_coredump: Updated `agent_debug_coredump` to return a detailed `instructions` text block along with `gdb_run_id`, explaining how to send commands with `agent_write_to_run` and warning the agent to terminate the session using `agent_terminate_run` when finished. Documented in `docs/tools.md` and added unit test coverage.
- Coredump Debugging Guidance in crashdump_get_info: Updated `crashdump_get_info` to detect if a coredump file is available (either locally under `crash_id` dump directory or in systemd `coredumpctl`) and dynamically append clear tool invocation instructions for `agent_debug_coredump` to guide the agent debugging flow. Cleaned up include paths to be relative to `src/` and verified correctness via unit tests.
- Coredump Capture and Debugging Pipeline: Implemented a hybrid coredump capture pipeline combining out-of-process child parent-tracing (`gcore` dump with Yama ptrace permission settings) with a `coredumpctl` fallback search based on the system crash ID. Created the new `agent_debug_coredump` tool to let AI agents trigger diagnostic GDB sessions on coredumps from the crash database, integrated it into the ncurses Editor UI dispatching, and wrote a dedicated unit test target `test_agent_debug_coredump.cpp`.
- `fs_replace_content` and `fs_replace_lines` VFS Support: Extended the line-replacement tools to support editing files hosted on local VFS providers (such as `tmp://` and `images://`). The tools inspect `is_local_path_available()` on the virtual file system to obtain the underlying absolute physical filepath using `get_local_path()` before performing read/write streaming operations.
- `fs_regexp_lines` VFS Support: Added full Virtual File System (VFS) URI support to the `fs_regexp_lines` tool, enabling agents to execute regular expression searches directly against virtual file buffers (e.g., `tmp://`). Integrated binary file detection for virtual files and updated the unit tests (`test_fs_regexp_lines.cpp`) to verify VFS regex match execution.
- VFS Audit & Support Analysis: Performed a comprehensive manual/agentic audit on all 20 `fs_*` tools, producing a structured analysis table and documentation mapping VFS URL capability across file traversal, compile-commands, and diagnostic modules. Documented findings in [fs_tools_vfs_analysis.md](file:///home/arjan/.gemini/antigravity-cli/brain/7bb29e59-e896-4a70-af36-5c3cd2f180f1/fs_tools_vfs_analysis.md).

## 19-07-2026
- `data_decompress` Tool `input_file` Parameter: Added an optional, dedicated `input_file` parameter (accepting local file paths and VFS URIs) to the `data_decompress` schema to resolve ambiguity caused by overloading `input_data`. Enforced mutual exclusivity validation between `input_data` and `input_file`. Added comprehensive test coverage to `test_binary_plugin.cpp` verifying local file extraction, slicing (offset/length), validation errors, and execution.
- Main System Prompt VFS Documentation: Added `tmp://` to the Virtual Filesystem (VFS) reference table inside the agent's main system prompt in `agent_window.cpp` so that LLM agents are explicitly aware of the sandboxed scratch space for diagnostic dumps, intermediate files, and summaries.
- Hexinspect and highlighter test suite reliability fixes: Fixed use-after-free and dangling pointer bugs in `test_hexinspect.cpp` by moving stack-allocated virtual file systems to the global `main()` scope. Fixed Elf section assertion checks to search for `.shstrtab` and relaxed PDF cross-reference table checks to support PDFs utilizing xref streams. Added explicit assertions verifying dimensions and color space metadata in the PNG/JPEG highlighter tests.
- Hexinspect overview summary tables and pagination: Implemented structured overview summary tables for ZIP, TAR, ELF, PDF, PNG, and JPEG highlighters inside `hexinspect`. ZIP and TAR archives support 15-line pagination previews and VFS temp file dumps (`tmp://archive_contents_<filename>.md`) when they exceed 40 lines. ELF, PDF, PNG, and JPEG (non-archives) default to `prefer_summary_in_tmp_only() == true`, dumping summaries directly to `tmp://inspected_structure_<filename>.md` and bypassing inline previews to save context. Added name-based object lookup to PDF using PDF object IDs. Added extensive new unit test cases inside `test_hexinspect.cpp`.
- Stuck agent/tool execution status matching fix: Replaced map-based `tool_call_id` matching in conversation loading and Gemini, Claude, and OpenAI model protocols with a robust sequential forward-scan tracking algorithm, resolving infinite loops caused by duplicate or reused tool call IDs.
- VFS Write Support: Implemented a robust C++ write interface (`vfs_writer`, `vfs_write_handle`) for the Virtual File System (`vfs_provider` and `virtual_file_system`). Implemented secure write support for the `file://` provider (enforcing canonical workspace boundary confinement to prevent path traversal escapes) and the `images://` provider (routing images to `images::image_manager` for automatic database ingestion and alias/URI mapping). Added a high-level `write_file(uri, data, size)` single-step blob shortcut to write entire payloads in one call. Simplified `test_tool_infrastructure`, `test_file_security`, `test_virtual_file_system`, `test_github_vfs`, `test_skill_manager`, and `test_sqlite_validation` targets in `meson.build` to compile cleanly and avoid duplicate symbol errors. Added comprehensive new unit tests in `test_virtual_file_system.cpp`.
- PDF decompression filters: Extended `data_decompress` (under `src/plugins/binary/binary_utils.cpp` and `data_decompress_validator.cpp`) to support `/LZWDecode` (with early change support as `"pdflzw"` or `"lzw"`), `/ASCII85Decode` (as `"ascii85"`), and `/RunLengthDecode` (as `"pdfrunlength"` or `"runlength"`). Implemented auto-detection of ASCII85 format via `<~` magic header. Added comprehensive unit tests in `test_binary_plugin.cpp`.
- Tool multi-family support: Implemented support for registering a single tool under multiple families using a `|`-separated family string (e.g. `"binary|hexedit"`). Updated `tool_validator` and `tool_registry` to split family strings and allow execution when any of the tool's families are active. Added unit test coverages in `test_activate_tool_family.cpp` to verify.
- Binary compression plugin: Implemented a new `binary` tool family featuring `data_compress` and `data_decompress` tools. Features auto-detection of zlib, gzip, zstd, xz, bzip2, and lz4. Supports files, base64, hex, and data URIs seamlessly, and allows precise byte-range extraction (offset/length) to dynamically unpack nested streams.
- PDF hex highlighter: Implemented `pdf_hex_highlighter` to parse and highlight PDF structures (objects, streams, cross-reference tables, and trailers) using a linear scanner approach robust to incremental updates and malformed offsets. Registered the highlighter in `hex_highlighter_registry` and hooked it into the build system. Added `test_pdf_highlighter` using a real PDF testcase (`shared-mime-info-spec.pdf`).

## 18-07-2026
- Editor window wide-character rendering fix: Fixed wide UTF-8 character cursor positioning and rendering in `src/ui/window.cpp` and `src/line.cpp`. Updated `char_to_display_col` in `src/line.cpp` to correctly resolve multi-byte character columns via `utf8::display_width` instead of hardcoding 1 column. Refactored the window line rendering loops in `src/ui/window.cpp` to map character layout widths dynamically, printing regular wide characters once at their display starting cells to eliminate overlapping duplicate rendering bugs. Corrected display assertions in `tests/unit/test_line.cpp`.
- Markdown inline formatting rendering in agent window: Implemented a robust state-machine parser in `wrap_text` to parse inline Markdown delimiters (`**`, `__`, `*`, `_`, `` ` ``). Consumed formatting tags into logical style attributes (`ATTR_BOLD` / `ATTR_UNDERLINE`) bound to individual characters, avoiding raw delimiter character count overhead in visual wrapping width calculations. Modified `agent_window` character drawing to map logical layout formatting onto ncurses attribute layers (`A_BOLD`, `A_UNDERLINE`). Retained original delimiter rendering intact inside fenced code blocks and inline code statements.

## 17-07-2026
- Persistent search/replace query & Configuration/Session Split: Implemented search query persistence across editor restarts. Refactored the architecture by splitting transient session state (such as last search and replace query terms) into a new `session_manager` using JSON serialization (`session.json`), while keeping declarative user preferences inside `config_manager` (`config.ini`). Created the C++ class `session_manager` (`src/session_manager.cpp` & `src/session_manager.h`) to handle loading and saving of the JSON configuration files (with project-level and global-fallback pathing). Added dedicated session unit test target `test_session_manager` (`tests/unit/test_session_manager.cpp`).
- ZIP file format introspection support: Created `zip_hex_highlighter` (`src/hex/zip.cpp` & `src/hex/zip.h`) to parse ZIP archives. It uses a robust Central-Directory-First parsing pass with a sequential scan fallback to identify local file headers, compressed data sections, central directories, and EOCD, and integrates with the `offset_by_name` parameter to resolve archived filenames to their raw offsets. Added unit test coverages in `tests/unit/test_hex_highlighter.cpp` using a relocated copy of `my-test-package.zip` inside `tests/`.
- Named chunk and symbol offset resolution in hex tools: Added `offset_by_name` (optional string parameter) to `hexdump`, `hexwrite`, and `hexinspect` tool arguments. Highlighters (`elf_hex_highlighter`, `png_hex_highlighter`, and `jpeg_hex_highlighter`) implement `get_offset_by_name` to lookup target offsets by section/symbol/chunk name (e.g. `".text"`, `"PLTE"`, `"APP0"`) dynamically.
- Out-of-process crash backtrace resolution: Created a standalone helper application `turbostar-crashprocess` (`src/crash_process.cpp`) that dynamically resolves virtual addresses in crash log files to function names, source filenames, and line numbers using `eu-addr2line` (with `--linux-process-map`) or standard `addr2line` as fallback. Configured `src/crash_handler.cpp` to use the async-signal-safe `fork()` + `execl()` + `waitpid()` sequence to execute this helper before cleanly terminating the crashed parent process.
- Executable path crash logging: Added absolute path logging of the running executable using the async-signal-safe `readlink("/proc/self/exe")` system call to both `src/crash_handler.cpp` and `src/crash_catcher/crash_catcher.c`'s crash loggers, facilitating post-processing symbolication (e.g. `addr2line`).
- Crash Catcher Enhancements Port: Ported the three recent enhancements from `turbocatch` (`src/crash_catcher/crash_catcher.c`) to Turbostar's internal crash logger (`src/crash_handler.cpp`). The logger now captures and prints `CrashAddress` (for SIGSEGV, SIGBUS, SIGFPE, and SIGILL), detects and logs read/write access types (using `uc->uc_mcontext.gregs[REG_ERR]`), and identifies specific SEGV failure types (`SEGV_MAPERR` / `SEGV_ACCERR`).
- Model switcher thread safety fix: Resolved a data race and use-after-free crash occurring when switching models in the middle of active LLM interactions. Converted `client_` inside `ai_agent` to `std::shared_ptr<llm_client>`, enforced the standard lock order (`conversation_mutex_` first, then `state_mutex_`) in `set_model`, and copied `client_` locally under `conversation_mutex_` before initiating stream/compaction requests in background threads.
- `fs_read_symbol` LSP precise symbol reading: Added a new `fs_read_symbol` tool accepting `path` and `symbol_name` parameters. The backend queries the running editor's `lsp_manager` for `textDocument/documentSymbol`, resolves nested/namespace/class scopes recursively, expands the symbol range by 2 lines on either side to prevent braces/signature truncation, reads the lines, and formats the output identically to `fs_read_lines`. Added integration tests to `test_tools.cpp` that spawn clangd end-to-end to verify lookup correctness.
- `fs_read_lines` tail reading: Added an optional `tail` parameter (integer) to `fs_read_lines` to read the last N lines of a file directly, eliminating off-by-one errors and extra roundtrips. Validated mutual exclusivity with `start_line` and `end_line` parameters. Updated `test_tools.cpp` to verify.
- `fs_list_dir` pagination and limit: Added optional `limit` (defaulting to 100) and `offset` (defaulting to 0) parameters to `fs_list_dir` to support systematic paginated traversal of large directories. Formatted a human-readable summary line (e.g. `*Showing files 100 - 150 out of <total>*`) in `fs_list_dir_entry.cpp` along with next-page command recommendations. Updated `test_fs_list_dir.cpp` to verify.
- `run_python` HTTP timeout configuration: Prepended `UV_HTTP_TIMEOUT=300` to the python execution command prefix in `run_python_entry.cpp` to prevent dependency installations from hanging indefinitely in offline or sandboxed environments.
- `fs_compile_summary` total summary row: Updated `fs_compile_summary_entry.cpp` to sum compiler errors, compiler warnings, LSP errors, and LSP warnings across all files and append a bolded `Total` row at the bottom of the table. Updated `test_fs_compile_summary.cpp` to verify.
- `fs_compile_project` clean & timeout parameter documentation: The backend implementation of `fs_compile_project` already supported `clean` (for forcing rebuilds) and `timeout` parameter args, but they were missing from the validator description and documentation. Added them to `tools.md` and updated `test_fs_compile_project.cpp` to verify.
- `fs_compile_file` single-file compile clarification: Prepended a note to the compilation output explaining that single-file compilation only checks syntax/errors and does not rebuild/link the project executable. Updated `test_fs_compile_file.cpp` to verify.
- `fs_regexp_lines` case insensitivity: Added a `case_insensitive` boolean parameter (defaulting to false) to `fs_regexp_lines` and configured RE2 options in `fs_regexp_lines_entry.cpp` to respect it. Corrected the tool documentation in `tools.md` to mention RE2 instead of `std::regex`. Updated `test_fs_regexp_lines.cpp` to verify.
- `git_log` count configuration: Refactored `git_log` tool to inherit from `tool_validator` instead of `zero_argument_tool_validator` to accept an optional `count` parameter (defaulting to 10), allowing agents and users to view arbitrary commit counts. Updated `test_git_log.cpp` to verify.
- `fs_man_search` keyword lookup tool: Implemented a new search tool inside the `fs_man` tool family that performs keyword searches on manual pages (similar to `man -k` or `apropos`). Features robust argument validation to prevent shell injection, results sorting, deduplication, and formatting as a clean markdown table (capped at 50 results to conserve context). Added integration test coverage in `test_fs_man.cpp` and registered the tool in `src/tools/meson.build`.
- Debugger auto-continue default fix: Modified `document_provider::start_app` and `editor::start_app` to accept an optional `auto_continue` boolean parameter (defaulting to `true`). Overrode the argument to `false` in `agent_start_app_entry.cpp` so that tool-based launches do not immediately run past initial breakpoints, while UI-based launches still respect user preferences.
- `exit_plan_mode` read-only state transition verification: Added assertions to `test_exit_plan_mode.cpp` confirming that read-only state transitions cleanly on enter/exit plan mode, resolving the concern that `exit_plan_mode` could retain the read-only state.
- `gemini_connection` role-alternation message grouping: Modified `gemini_connection.cpp` to automatically merge consecutive user/tool/assistant messages mapping to the same Gemini API role into a single grouped content object with multiple parts. This prevents "consecutive messages must alternate role" API errors during session restarts or when multiple pending tool calls are present in the history.
- `git_branch_list` exit code message leak fix: Added line filtering in `git_branch_list_entry.cpp` to ignore `"Process exited with code"` lines generated by command sync executions. Updated `test_git_branch_list.cpp` to verify.
- `httplib_transport` thread-safety and empty host guard: Added defensive empty host check in `httplib_transport` constructor to prevent null-pointer dereference crashes, and added `cancelled_` atomic checks to safely cancel HTTP streams from the callback execution thread without triggering OpenSSL socket-closing crashes.
- `meson.build` application binary auto-detection: Added automatic main executable detection in `config_manager` by parsing `meson.build` in the project root if no binary is configured, making "Run Program" / "Run in Debugger" immediately active in new project workspaces. Handled environment bypasses with `TURBOSTAR_NO_AUTO_DETECT` and updated tests.
- `list_tool_calls` enhancements: Added `search` substring filtering (case-insensitive) and `show_details` parameter schema detailed output formatting. Added unit test cases to verify filtering and schema structures and updated `docs/tools.md` documentation.
- `image_getdata` tool: Built a new tool inside the basic image operations plugin that retrieves the binary content of a VFS image (either via URI or alias) and returns it as a Base64-encoded Data URL. Relocated MIME type detection (`detect_mime_type`) and binary output formatting (`format_binary_output`) to the shared `fs_utils` utility module to eliminate duplication. Integrated buffer-based MIME detection (`utf8::detect_mime`) to determine the MIME type directly from the image data in memory, falling back to original extension parsing when needed. Added test coverage in `test_image_tools.cpp` and fully documented the tool in `docs/tools.md`.
- MIME detection centralization: Created a unified `mime::` namespace in `src/mime.h` and `src/mime.cpp` to centralize all MIME and file type description logic. Replaced all separate inline detection logic and direct `magic_compat.h` libmagic invocations in `utf8.cpp`, `fs_utils.cpp`, `image_manager.cpp`, `fs_list_dir_entry.cpp`, and `hexinspect_tool.cpp` with delegations to the new central helpers. Added signature-based fallback detection for common formats and file existence checking to prevent non-existent files from returning error messages as MIME types. Updated `src/meson.build` and main `meson.build` to compile and link `mime.cpp` to all relevant targets.

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