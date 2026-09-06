# Web page todos (in `docs/index.html`, `docs/ai.html` and `docs/editor.html`)

## Webpage Screenshots to Take
- [ ] Screenshot for `ai.html`: Agent using `hexinspect` to analyze a binary file in the chat (`screenshot-agent-hexinspect.png`)
- [ ] Screenshot for `ai.html`: Agent extracting an asset from a container like a PDF or ZIP (`screenshot-agent-container-extract.png`)
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

- add "grep_search" to the tool alias table for fs_grep_files

- `fs_run_tests` suite filter (`suite: "unit"` / `"e2e"`): allow running only fast local unit tests without invoking slower external/e2e test suites to prevent 3-minute MCP timeouts when building and running tests.

- feature: we have "yolo" mode already -- add a /yolo slash command to the agent window

- `hexdump` symbol size auto-resolution: when `hexdump` is called with `offset_by_name` (e.g. symbol name `"main"` or section `".text"`), automatically resolve `size` from the symbol's size reported in the binary `.symtab` when `size` is omitted or unspecified.

- `hexinspect` format auto-chaining: when `hexinspect` is invoked on a compressed archive (e.g. `.tar.gz`, `.gz`, `.bz2`, `.zst`), transparently decompress the stream in memory before running structural format parsers (TAR, ELF, PNG, JPEG).

- consider async compile of the whole project on edits to cut down time?
	- both editor and agent env
	- needs to be abortable and error/warning preserving for the "real" compile

- we need to handle better the case where the server says we exceed token window by doing emergency compaction
- solve compaction better overall

- if you are in the agent window, and go to the file menu, and hit <ESC>, the menu never goes away
	- it's not actually that simple.. cannot reproduce easily

- toolcall:// namespace that lets us nest toolcalls?

- `fs_replace_lines` dry-run verification: perform dry-run verification against `original_text` for all batch edits in `fs_replace_lines` before applying any mutations. If any line check fails after accounting for previous edits, reject the batch cleanly to prevent partial line-drift edits.


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
	- language specific guidelines?

- meta feature: helper agents
	- well rounded subagents that have custom tools available to it for specific higher level tasks, can be used by the main agent as if they are fancy tool calls
		- we started this with code review kind of
	- should be able to suggest model names/capabilities to be run in -- say a vision model for image processing tasks
	- [x] should be able to register /slash commands
	- [x] skills that plug into specific subagent types only
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


# Done

## 6-09-2026
- Streamlined crashdump backtrace reporting: fixed whitespace trimming bug in GDB function signature parsing (`find_last_of` -> `find_last_not_of`) that previously truncated arguments and parentheses; stripped `<optimized out>` arguments from function signatures across all backtrace frames to eliminate token bloat and line wrapping; collapsed contiguous leading crash-handling frames into a single summary row (`0-4`); accurately tagged external locations (`<libc>`, `<turbocatch>`, `<external>`); omitted addresses for non-project external frames; and removed redundant 'Frame Note' column from Codemap Summary table to save tokens and eliminate frame desynchronization. Added unit tests in `tests/unit/test_crashdump_get_info.cpp`. (Completed)

## 5-09-2026
- Compiler health check ANSI sanitization & exit code verification: sanitized raw compiler output in `update_file_health_state` via `utf8::sanitize_terminal_output` to strip ANSI escape sequences emitted by `-fdiagnostics-color=always` before matching `: error:` and `fatal error:`; verified `Process exited with code 0` to reliably catch compile failures during single-file health checks; added diagnostic event logging in `mcp_mode` and expanded `tests/unit/test_file_health_tracker.cpp` with an RAII-guarded clean-to-dirty compiler transition test. (Completed)
- `fs_read_lines` 'length' parameter & aliases: added `length` parameter to `fs_read_lines` specifying the count of lines to read starting at `start_line` (defaults to 1 if omitted), with parameter aliases `num_lines`, `line_count`, and `lines_count`. Enforced mutual exclusivity with `end_line` and `tail`, positive non-zero validation, and overflow-safe end line calculation. Updated `docs/tools.md` and added unit test coverage in `tests/unit/test_tools.cpp`. (Completed)
- Tool parameter aliases & LSP teardown hardening: implemented generic parameter alias rewriting at `agentlib::tool_validator` level mapping parameter variations (`file_path`, `filepath`, `filename`, `file`, `target_file` -> `path`; `count`, `max_results` -> `limit`; `args` <-> `arguments`) prior to JSON schema validation, with custom per-tool alias customization hook; resolved teardown hang on exit by fixing `lsp::Process::Impl::read()` EOF handling on closed pipes, ignoring SIGPIPE on subprocess write, and converting `lsp_manager::global_queue_` to atomic pointer reset in `lsp_manager::stop()`. Added unit tests in `tests/unit/test_tools.cpp`. (Completed)
- Edit error attribution and MCP state persistence: resolved edit sequence reset (`[Edit ID: #1]`) in `turbomcp_server` by persisting `tool_context` as a class member across consecutive MCP tool calls; in `fs_compile_project`, integrated `agentlib::get_all_file_health_attribution_notes` to append diagnostic attribution notes identifying which edit first introduced compiler errors on build failure, and reset file health states on successful build; added unit tests in `tests/unit/test_fs_compile_project.cpp` and `tests/unit/test_turbomcp_server.cpp`. (Completed)
- Unified `run_executable` tool: replaced and renamed `agent_start_app` with a unified `run_executable` tool permitting execution of any binary within the project directory workspace (with or without `build/` prefix, such as `"crash"`, `"build/crash"`, `"./build/crash"`, `"turbostar"`), while strictly rejecting binaries outside the project directory (e.g. `/bin/ls`) or path traversal attempts. Added parameter aliases (`arguments` / `args`, `executable` / `path` / `binary`). Updated `document_provider` and `editor::start_app` to validate project workspace containment and resolve binary paths. Updated companion tool descriptions (`agent_get_profile_summary`, `agent_get_profile_details`, `agent_wait_for_app`, `agent_write_to_run`, `agent_get_run_screenshot`, `agent_terminate_run`), `docs/tools.md`, `docs/design-perf-integration.md`, `docs/ai.html`, and added unit tests in `tests/unit/test_run_executable.cpp`. (Completed)
- Code review infrastructure polish (`item_id` standardization, batch resolution, and state filtering): standardized code review item tools (`get_code_review_item`, `resolve_code_review_item`, `confirm_code_review_item`, `update_code_review_item`) to use `item_id` as the primary parameter name while accepting `id` as an alias; enabled batch resolution in `resolve_code_review_item` supporting `item_ids: [1, 2, ...]` to resolve multiple items in one call; added optional `state` parameter to `list_code_review_items` (`active`, `new`, `confirmed`, `disputed`, `stale`, `resolved`, `verified-fixed`, `all`) with role-aware visibility; updated git commit reminder message and `docs/tools.md`; expanded unit tests in `tests/unit/test_resolve_code_review_item.cpp`, `test_get_code_review_item.cpp`, `test_confirm_code_review_item.cpp`, `test_update_code_review_item.cpp`, and `test_list_code_review_items.cpp`. (Completed)
- `fs_list_dir` robustness & symlink permissions hardening: in `fs_list_dir_entry.cpp`, wrapped per-entry disk scanning in an inner try/catch and non-throwing `std::filesystem` error_code calls so unreadable files or broken entries do not abort the entire directory listing; queried `symlink_status` for symlinks instead of following target permissions; sanitized directory names in markdown headers; added `additionalProperties: false` to parameter schema; added unit tests for unreadable files, broken symlinks, and empty directories in `tests/unit/test_fs_list_dir.cpp`. (Completed)
- `fs_list_dir` project-relative wildcard resteer: in `fs_list_dir_entry.cpp`, piped paths through `fs_utils::make_relative_to_project` so that error messages and `fs_find_files` wildcard suggestions surface clean workspace-relative paths instead of absolute system paths. Added unit test assertion in `tests/unit/test_fs_list_dir.cpp`. (Completed)
- `fs_find_files` hybrid pattern default & `fs_list_dir` cross-suggestions: defaulted `pattern` to `"*"` in `fs_glob` / `fs_find_files` when an explicit directory path is targeted (preventing validation aborts on directory browsing), while requiring a pattern at root to prevent whole-repo dumps; added contextual suggestions across `fs_find_files` and `fs_list_dir` (including wildcard detection in `fs_list_dir`). Added unit test coverage in `tests/unit/test_fs_glob.cpp` and `tests/unit/test_fs_list_dir.cpp`. (Completed)
- `fs_grep_files` non-existent path alternative suggestions & directory validation: fixed uncaught filesystem error when `search_path` does not exist by adding early path validation in `fs_grep_files_entry.cpp`, returning a clean error with `filename_suggest_alternative` suggestions. Added explicit directory vs. special file check before `recursive_directory_iterator`. Added unit test coverage in `tests/unit/test_fs_grep_files.cpp`. (Completed)
- `fs_utils::filename_suggest_alternative` & `fs_read_lines` alternative suggestion: implemented `filename_suggest_alternative` in `src/fs_utils.cpp` / `src/fs_utils.h` to locate existing project files with matching basenames when a requested file path does not exist, ranking candidates by longest common directory prefix. Integrated into `fs_read_lines` error reporting to suggest closest matching alternatives on ENOENT. Added unit test coverage in `tests/unit/test_fs_utils.cpp`. (Completed)
- `fs_run_tests` verbose flag & "auto" mode: added `verbose` argument (`"auto"`, `"true"`/`true`, `"false"`/`false`) to `fs_run_tests`. Defaults to `"auto"`, where single test executions suppress verbose logs when passing and surface full verbose logs (stdout/stderr) on failure. Explicit `true` always surfaces verbose output, and `false` keeps output compact. Conditional on build system (`meson test -v` / `--print-errorlogs` for Meson; `ctest -V` / `--output-on-failure` for CMake). Documented in `docs/tools.md` and added unit test coverage in `tests/unit/test_tools.cpp`. (Completed)

## 4-09-2026
- `perform_code_review` async by default & documentation: changed default execution mode of `perform_code_review` to `async=true` in `perform_code_review.h` and `perform_code_review_security.cpp` to prevent MCP/RPC client timeout aborts during multi-stage reviewer + verifier passes; documented `perform_code_review` in `docs/tools.md` under Section 11 (Code Review Tools); updated unit tests to verify default async behavior. (Completed)
- Fixed review items #271 & #272: updated `test_crashdump_get_info.cpp` to explicitly print skipped status for GDB enrichment when no coredump exists and corrected schema validation comment on unexpected arguments. (Completed)
- Fixed review items #269 & #270: secured `crashdump_manager` query accessors (`get_crashdumps`, `get_crashdumps_for_cookie`) with `std::lock_guard<std::mutex>` returning vector by value to eliminate data races with concurrent refreshes/clears; in `crashdump_get_info`, deterministic chronological selection of the newest crash by timestamp/mtime or highest numeric crash ID instead of directory iterator order. (Completed)
- Single-crash direct toolcall notification & optional `crash_id`: when exactly 1 crash occurs during execution across `fs_run_tests`, `fs_compile_project`, `fs_compile_file`, and `run_python`, automatically refresh crashdumps and notify the agent with the direct call including the exact `crash_id` (`Please use 'crashdump_get_info' with crash_id '<id>' to investigate stack trace and details.`). When multiple crashes occur, recommend `crashdump_list`. In `crashdump_get_info`, made `crash_id` optional, defaulting to the latest crash. (Completed)
- Test watchdog signal handler chaining: chained `test_watchdog` crash handlers to previous handlers (e.g. `libturbocatch.so`) so watchdog unwinds the stack while allowing `libturbocatch` to capture full crashdumps (registers, maps, backtrace, coredump) during assertion aborts and fatal crashes. (Completed)
- `fs_read_lines` mini class context preview: when reading class method implementations in C/C++ files, automatically queries LSP definition to locate the class header, extracts referenced member variables (up to 10) and sibling methods (up to 5) actually used in the read lines, and presents them in a formatted Class Context code block (`<line_num>: <code_text>`) above the codemap table. (Completed)
- `fs_glob` directory scoping and aliases: added optional `path` parameter (with aliases `search_path`, `directory`) to `fs_glob` allowing scoping to subdirectories or VFS URIs without pruning `build` or `tmp` directories when explicitly targeted; registered `fs_find_files` as an alias tool in the tool registry; updated `run_shell_command` resteer to recommend `fs_glob(pattern=\"...\", path=\"...\")`. (Completed)

- `fs_grep_files` inline definition snippet: when LSP symbol matches are found in `fs_grep_files`, automatically extract and include up to 3 lines of definition / prototype code in the standard `fs_read_lines` fenced code format (`<line_num>: <code_text>`) so agents can inspect signatures without extra read turns. (Completed)

- In `fs_grep_files` results, better LSP output: when 0 textual occurrences are found, output `No textual matches found for '<pattern>'` first and label LSP symbols clearly as `Related Symbol Index Definitions (no textual occurrences in searched scope)` so agents are not misled into thinking text matches occurred. (Completed)

- `fs_run_tests` string/array parameter flexibility: accept `test_names` as either a single string or an array of strings in tool arguments. (Completed)

- `fs_read_lines` single-sourced `tail` handling: unified `tail` calculation into `read_from_disk`, `read_from_vfs`, and `read_from_document`, eliminating duplicated file opening, stat checks, and double line-counting in `execute()`. (Completed)

- Accurate file-not-found error messages: fixed misleading `FIFO/device` error messages when accessing nonexistent files across `fs_read_lines`, `fs_file_size`, `open_in_editor`, and `fs_read_symbol` by properly distinguishing `ENOENT` / file non-existence and directories from actual non-regular FIFO/device files. (Completed)

- `fs_run_tests` tokenized fuzzy resolution: tokenized test matching splitting on punctuation (`_`, `:`, `/`, `-`), normalizing case, and ignoring noise words (`test`, `tests`, `turbostar`). Auto-runs if exactly 1 high-confidence match with an informative note (e.g. `unit_test_utf8` -> `turbostar:unit_utf8`), or provides suggestions if 2-5 candidates match. (Completed)

- Tool argument aliases & defaults: added `query` parameter alias to `fs_glob` and `fs_grep_files`, and made `path` optional in `git_diff_unstaged` and `git_diff_staged` defaulting to project root `"."`. (Completed)

- Crashdump vs GDB backtrace correlation: enriched crash reports with headless GDB coredump backtrace extraction in `crashdump_manager`, matching addresses with unwinder raw IPs, relativizing source paths to project workspace for symbol codemap integration, annotating snapshot frames as `crash handling`, and falling back to unwinder table if coredump is missing or GDB fails. (Completed)

## 1-09-2026
- Performance profiling Markdown formatting: format `agent_get_profile_summary` and `agent_get_profile_details` output as Markdown tables by default (`format="markdown"`), while supporting optional `format="json"` for machine-parsable JSON automation. (Completed)

- Single-file compile after edit: centralized `-fsyntax-only` single-file compile helper in `fs_utils` (`get_compile_command_for_file`), stripping object `-o` targets and dependency flags (`-MF`, `-MD`). Integrated into `update_file_health_state` across all file edit tools (`fs_replace_content`, `fs_replace_lines`, `fs_multi_replace_content`, `fs_write_file`) for deterministic compiler-driven failure attribution per `Edit ID #N`. (Completed)