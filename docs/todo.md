# Web page todos (in `docs/index.html`, `docs/ai.html` and `docs/editor.html`)

## Webpage Screenshots to Take
- [ ] Screenshot of the website homepage running in a browser showing the retro-modern Turbo Pascal HSL color scheme
- [ ] Screenshot of the compile context optimizer output in the terminal showing raw build logs vs. minimized diagnostics
- [ ] Screenshot of an agent interactive debugging session with parallel GDB execution and stdin interactions
- [ ] Screenshot of an inline agent running automated security scans via `bandit` on a C++/Python file

## Webpage todo items (do not delete this header line)



# short term fixes -- not in priority order, agents can add and remove items as they come up (do not delete this header line)

- feature: a markdown_to_html filter 
	- should be straightforward structural conversion
	- almost a line by line regexp, after some boilerplate headers / footers



- nit: the hexeditor has a size limit that's a bit on the small side -- maybe we should check system memory size and on large systems
    increase the limits?

- feature: subagent creation wizzard dialog

- feature: we have a groff-to-markdown converter - this could be another filter

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
	- not just the agent tool -- also in the editor!

- code review enhancements
	- we can provide upfront a set of static analysis data to the code review agent
		- known compile / LCP warnings
		- cyclic complexity scores?
		- cpplint or other similar tools
		- -fanalyzer output ? should maybe add to fs_compile_file when in code review agent
		- for python we have our security analyzer -- we should run this when reviewing python files
		- if we had backtraces/crashes that involve this file we should include them in context or at least reference them with the toolcall to get the details

- feature: we should create a code review agent file in agents/
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
			- with persistent cache, quick conversion followed by background slower-but-more-accurate conversion
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

## 04-07-2026
- Dynamic skill plugins: Implemented support for "hidden" (invisible) skills by adding a visibility toggle, enabling plugins to register skills silently without polluting the user/agent enumeration. Added `ai_agent::activate_skill` for centralized, consistent programmatic skill activation that automatically formats and injects instructions into the agent's interaction history as a system message. Added `register_skill` overloads in `skill_manager` supporting dynamic registration of both simple single-string skills and complex multi-file skills mapped to string maps.
- Input history file path: Fixed a bug where `.turbostar_input_history.json` was incorrectly written to the project's repository root instead of the project-specific cache root (`~/.cache/turbostar/projects/...`), avoiding repository pollution. Added a dedicated unit test `test_input_history` to verify correct file path creation.
- Matt Pocock Skills plugin: Implemented a new dynamic shared module plugin (`mattpocock`) registering a hidden `grill-me` skill and binding it to the new `/grill-me` slash command, using Matt Pocock's industry-leading productivity instructions.
- libturbocatch installation: Configured `libturbocatch.so` to install to `$prefix/libexec/turbostar/` in `meson.build` and implemented robust path lookup prioritizing `TURBOSTAR_TURBOCATCH_DIR` environment variables, `TURBOCATCH_DIR` compilation macros, build root fallbacks, and system search paths. Added the build-root environment variable to unit tests and E2E runs.
- `apply_text_filter` agent tool: Implemented a new generic agent tool allowing the LLM/Agent to apply registered text processing or format conversion filters (e.g. `strip_utf8`, `strip_ansi`, `html_to_markdown`, `markdown_align_tables`) to any input text. Supports returning the converted string directly or saving it directly to a safe workspace file. Includes robust Stage 1 JSON validation, filter name enumeration on error, and a comprehensive unit test suite.
- Image capability core infrastructure: Designed and implemented the C++ core `images://` virtual file system (`image_manager` singleton) under `src/images/` to support content-deduplicated image caching (stored by SHA-256 in the project cache directory) and logically separated VFS addressable views (`by-sha256/`, `by-file-id/`, `by-name/`). Added modern OpenSSL 3.0 EVP-based file hashing, native PNG/JPEG dimensional metadata extraction, MIME detection, and dynamic plugin helper APIs (temp path generation and ingestion). Created a comprehensive unit test suite `test_image_manager`.
- Multimodal VFS Integration & Interception: Glued the `images://` VFS into connection protocols (OpenAI Completions/Response, Claude, Gemini) to translate URIs into base64 multimodal blocks. Added automatic post-response interception in the editor's main agent processing to extract, decode, cache, and replace raw inline base64 images with clean VFS URIs in conversation history.
- Image Manager TUI Dialog: Implemented a Turbo Pascal style dialog (`create_image_manager_dialog()`) for managing the `images://` VFS. Renders a scrollable list of VFS images with a dynamic metadata pane, thumbnail preview space, and interactive modal actions for Importing (via file selector), Saving (exporting to real files), and Deleting cached VFS entries. Accessible via the main menu bar under `Agent -> Image VFS Manager...`.
- Image Basic Operations Plugin: Implemented the `image-basic` shared module plugin, dynamically registering the `image` tool family and the `image_resize`, `image_crop`, `image_rotate`, `image_mirror`, `image_grayscale`, and `image_threshold` tools. Uses GraphicsMagick++ to scale images, crop selection rectangles, rotate, mirror, convert to grayscale, and apply standard/adaptive threshold binarization (ideal for OCR prep). All tools support an optional `output` parameter to redirect modified outputs to a new image alias, or default to updating the target alias in-place. Declared GraphicsMagick++ as an optional dependency so the plugin is dynamically built only when the library is detected on the system.
- Image Import and Export Tools: Implemented `image_import` and `image_export` as built-in tools in the application core to ensure basic VFS operations are always functional even without GraphicsMagick++. `image_import` loads local files or downloads web URLs (using curl and sandboxed domain verification) into the virtual VFS, and `image_export` writes cached images back out to workspace files. Both tools feature robust bare-name normalization (allowing `my-image.png` alongside full `images://by-name/my-image.png` paths).

## 03-07-2026
- Central filter registry: Implemented a decoupled `filter_registry` singleton allowing dynamic plugins and host code to register thread-safe content processing filters (e.g., `html_to_markdown`, `html_to_markdown_plain`, `html_extract_tables`, `markdown_align_tables`, `meson_compile`, `meson_test`, `strip_ansi`, `strip_utf8`). Enhanced the `web_fetch` tool with an optional `filter` argument to lookup and apply filters before returning or saving results.
- Web fetch file saving: Added `output_path` parameter to the `web_fetch` tool allowing agents to download and save fetched HTTP/HTTPS content directly to a workspace file, complying with standard file write permission rules.
- HTML links, images, tables, and text extraction: Implemented an optional `html` plugin with the `html_extract_tables`, `html_list_links`, `html_list_images`, and `html_extract_text` tools using `lexbor`, supporting active heading hierarchies (H1 to H3), table captions, list formatting, bold/italic toggles (Option C), pipe escaping/sanitization, aligned markdown table outputs, and parameter/size validation.
- Hexinspect tool migration: Moved and renamed the `hex_inspect_range` tool to `hexinspect` under the `hexedit` plugin family, updated all references, unit tests, and traffic playbacks, and fixed host symbol linkage for plugins.
- Editor: Add a section about our built-in hex editor, using the screenshot in `docs/hexeditor.png`.
- HTML syntax highlighter: Implemented a state-machine based C++ syntax highlighter for HTML (`html_highlighter`), added unit tests, and registered it in the editor.
- Hex editor split: Refactored and split file format highlighters into dedicated classes under `src/hex/`.
- JPEG/JFIF support: Added APP0 headers, SOF0/SOF2 frame dimensions, and entropy scan segment parsing for JPEG/JFIF files to the hex highlighter.
- Hex disassembler: Shortened long hexadecimal address/offset/immediate representations in disassembler outputs by stripping leading zeros.
- Hex editor plugin: Implemented `hexedit` shared module plugin registering the `hexedit` tool family with `hexdump` and `hexwrite` tools, complete with file format metadata annotation integration and a dedicated unit test suite.

## 02-07-2026
- implemented a dynamic C++ /command registry (`agent_command` and `command_registry` singleton) for agent slash commands, removing over 500 lines of duplicate hardcoded switches from `agent_window.cpp` and making slash commands extensible for plugins.
- implemented input history for text input boxes and multiline prompt edits, persistent per project.
- implemented ^K R shortcut inside ui_multiline_edit to prompt for and read/insert external file contents.
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