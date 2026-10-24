# Turbostar top level design documentation

Turbostar is a TUI text editor, with the look and feel of Turbo Pascal, but
with wordstar keybindings ("joe" dialect -- if wordstar and joe conflict,
joe behavior dominates)

# Update policy

Keep this file, and all documents described in the "Other Documents"
sections updated as enhancements are made to the project.

# Other documents

| Document               | Description                |
| ---------------------- | --------------------       |
| `docs/style.md`        | Coding style               |
| `docs/general-c++.md`  | Coding style for C++       | 
| `docs/design-ui.md`    | UI architecture and widgets|
| `docs/keybindings.md`  | Key binding documentation  |
| `docs/colorscheme.md`  | Color scheme documentation |
| `docs/buttonrecipe.md` | How to make UI buttons     |
| `docs/test-guidelines.md` | End-to-End test guidelines |
| `docs/llmtools.md`     | LLM Tool Infrastructure    |
| `docs/design-gdb-io.md` | GDB IO architecture        |
| `docs/mcp.md`           | Model Context Protocol (MCP) design |
| `docs/thread-lifecycle.md` | Thread lifecycle and teardown blueprint |
| `docs/code-review.md`  | Code review system and agent tools |
| `docs/design-plugin.md` | Plugin architecture, lifecycles, and tool registration |
| `docs/design-subagents.md` | Subagent Library and Manager architecture |
| `docs/design-website.md` | Project website structure, styling guidelines, and update rules |
| `docs/design-selection.md` | Dual-mode selection architecture and specification |



# Git policy

After making a change, create a git commit for the change. Before committing, ensure the code is formatted by running `clang-format -i src/*.cpp src/*.h`.

# Technical decisions

- Turbostar uses C++, the C++23 language version
- Turbostar uses the meson build system
- **Binary Invocation Targets**: The application can be invoked under two names: `turbostar` (which starts the standard TUI text editor layout) and `turboagent` (a symlink that launches the application in agent-first mode, immediately opening and focusing the AI Agent window, skipping the welcome screen and workspace restoration unless files are explicitly passed as arguments).
- **Virtual Filesystem (`system://` VFS)**: Provides an in-process URI scheme (`system://`) for demand-loaded system prompts, language coding standards (`system://languages/cpp23.md`, `system://languages/python311.md`, `system://languages/c17.md`, `system://languages/rust2021.md`, `system://languages/typescript.md`, `system://languages/verilog.md`, `system://languages/systemverilog.md`), workflow guidelines (`system://workflows/code_review.md`, `system://workflows/plan_mode.md`), and dynamic runtime index generators (`system://agents.md`, `system://tools.md`, `system://mcp.md`). Static markdown files in `system/` are compiled into header constants (`src/system_docs_embedded.h`) via `scripts/embed_system_docs.py`. Supports root fallback aliases (e.g., `system://cpp23.md`), project/user overrides (`.turbostar/docs/`), and Turn 1 auto-injection based on the project's configured `primary_language` setting.
- Each class gets its own .cpp file with a matching header file
- Data classes and presentation classes are strictly separated. All visual, windowing, and rendering components (e.g., `window`, `dialog`, `menu_bar`) reside within the `src/ui/` directory to enforce this separation.
- ncursesw is used for creating the TUI
- RAII memory management paradigm throughout
- Design goal: responsiveness 
- Multi-tier Locking Model:
    - **Document-level**: Each `document` uses a `std::shared_mutex` to protect the structure of the document (the list of lines) and the cursor/selection state. `shared_lock` is used for rendering and reading state, while `unique_lock` is used for structural changes or moving the cursor.
    - **Line-level**: Each `line` uses its own `std::shared_mutex` to protect its internal UTF-8 string data. This allows background threads to process different lines of the same document concurrently without blocking the main document or each other.
    - **Lock Ordering**: To prevent deadlocks, the locking order is always **Document -> Line**.
    - **Snapshots**: `line::get_text()` returns a `std::string` copy to provide a thread-safe snapshot of the line content to readers.
    - **Compile-Time Thread Safety**: Clang's Thread Safety Analysis annotations (defined in `src/thread_annotations.h` via `safe_mutex`, `safe_lock_guard`, `safe_unique_lock`, `GUARDED_BY`, `REQUIRES`, etc.) are used across the codebase to statically verify locking discipline at compile time under Clang (using `-Wthread-safety`).
  - Per-Window/Document Event Queues: Thread-safe queues for passing events between threads.
- **Selection Model**:
  - Turbostar implements a strictly decoupled **Dual-Mode Selection Model** (see `docs/design-selection.md` for full specification):
    1. **Persistent Block Selection** (WordStar/Joe style): Document-scoped markers, `Selection Begin` (^KB) and `Selection End` (^KK), define a contiguous range of text that stays pinned to logical text across edits and navigation. Active only when both markers are set and start < end; incomplete marker states do not highlight. Operates on by `^K` commands (`^KC`, `^KM`, `^KY`, `^KW`, `^KJ`, and `^KT` to copy block to clipboard).
    2. **Transient Mouse Selection**: Window-scoped click-and-drag range that supports vertical auto-scrolling across screens, auto-copies to system clipboard via OSC 52 on mouse release, overrides block highlighting visually during interaction, and is cleared by any subsequent keypress or click.
- **Stateful Key Sequences**:
  - Turbostar supports "prefix" keys, most notably the `Ctrl-K` block.
  - When a prefix key is pressed, the editor enters a sub-state waiting for the next "command" key.
  - This enables a large number of commands while keeping the primary keymap uncluttered.
- **Visual Colors & Highlighting**:
  - Turbostar uses a 16-color palette (base colors + 8 for high intensity).
  - Explicit color pairs MUST be used for all UI elements and highlights.
  - **Avoid `A_REVERSE`**: Never use the `A_REVERSE` attribute to highlight focus or selection. Instead, always allocate and use a dedicated color pair (e.g., Black on Green). This ensures visual consistency and predictability across different terminal environments.
- **UTF-8 Handling and Helper Functions**:
  - All UTF-8 helper functions must be consolidated inside the `utf8` namespace (declared in [src/utf8.h](file:///home/arjanvandeven/git/turbostar/src/utf8.h) and defined in [src/utf8.cpp](file:///home/arjanvandeven/git/turbostar/src/utf8.cpp)).
  - Inline byte parsing, character length parsing, or offset conversion logic should NEVER be written ad-hoc. Existing helpers like `utf8::char_len`, `utf8::length`, `utf8::char_to_byte_offset`, `utf8::byte_to_char_pos`, and `utf8::next_character` must be reused.
  - **Hard Rule**: If a new UTF-8 helper is ever needed, it **MUST** go into `src/utf8.h` / `src/utf8.cpp` to prevent duplication and ensure correctness across the codebase.
- **Fallback Crash Handling**:
  - A fallback crash handler using `libunwind` is installed in `main.cpp` for fatal signals (`SIGSEGV`, `SIGABRT`, etc.) and uncaught C++ exceptions. It outputs diagnostics directly to `stderr` and a structured crash log file (`~/.cache/turbostar/crashes/crash_<pid>_<timestamp>.txt`).
  - Prior to installation, it queries the active `SIGSEGV` handler; if any custom handler is already registered (such as by `libturbocatch.so`), it automatically steps back to respect it.
  - On crash, the fallback handler writes raw ANSI escape sequences directly to `stderr` to reset raw mode, show the cursor, and restore terminal colors to prevent garbled console states.
  - **Diagnostic Context**: Crash reports include the triggering thread ID and thread name (set via `fs_utils::set_current_thread_name` across all editor threads), thread-local active context breadcrumbs (`crash_handler::scoped_breadcrumb`), full general-purpose CPU register state (`RAX`..`R15`, `RIP`, `RSP`, `EFL`), and recent in-memory log entries.
  - **Memory Region Classification**: Post-mortem helper `turbostar-crashprocess` parses the parent process's `/proc/<pid>/maps` to classify the fault address: reporting whether it falls inside a mapped region (with permissions and offset) or unmapped space (computing distance to stack pointer `RSP`, heap boundaries, and null dereference proximity).
- **Project Directory Override**: The active project directory (normally resolved dynamically from the git tree root) can be overridden by setting the `TURBOSTAR_PROJECT_ROOT` environment variable. This allows C++ and E2E tests to run in isolated staging areas to prevent resource conflicts and allow parallel execution.
- **Tool Tracing (`--tooltrace`)**: Developers can pass `--tooltrace` on startup to trace all LLM tool executions to sequential log files (`toolcall.0`, `toolcall.1`, ...) in the current working directory. Each log file records the tool call JSON object, followed by a `---` separator line, followed by the exact tool output returned to the model.
- **1-Line Crash Summaries**: When application crashes are detected, `crashdump_manager` post-processes `assertion.txt` (written by `libturbocatch.so`) or top resolved DWARF stack frames to extract a 1-line summary string (e.g. `Summary: assertion fail at src/editor.cpp:42 'c != NULL' in update()`). This summary is included in crash notification messages appended to tool responses, giving the LLM immediate root-cause visibility without requiring a separate `crashdump_get_info` invocation. Uninformative crashes (unresolved address offsets or glibc startup frames) omit the summary line cleanly.
- **Hybrid GDB Coredump Backtrace Enrichment**: When generating `report.md`, `crashdump_manager` checks if a coredump file exists and GDB is available. If present, it invokes headless GDB (`--batch -ex "set width 0" -ex "set style enabled off" -ex "bt 35"`) to capture the full runtime call stack, parameter values (e.g. `foo (c=0x0)`), and accurate source lines. Instrumentation frames up to `<signal handler called>` are collapsed into a single summary row (e.g. `| 0-4 | | <crash handling frames> | <turbocatch> | crash handling |`) to eliminate distraction while preserving frame numbering alignment with interactive GDB (`agent_debug_coredump`). Arguments containing `<optimized out>` are stripped across all frames to minimize token bloat and prevent line wrapping. Locations outside the project workspace are accurately tagged (`<libc>`, `<turbocatch>`, or `<external>`), and library addresses are omitted to focus on user code. Source paths are normalized relative to the project workspace to drive the compact Codemap summary. If no coredump exists or GDB fails, the report seamlessly falls back to the unwinder table.



# Architecture

## Document class

The document class represents a whole file and serves as the primary "Model" in our MVC-like architecture.

- **Data Structure**: `std::vector<Line>` where each `Line` represents a single line of text.
- **Undo/Redo**: Implemented via a transaction log. Each transaction stores deltas at the `Line` level to balance implementation simplicity with memory efficiency.
- **Concurrency**: Each document will be protected by a `std::shared_mutex` (RW-mutex) to allow multiple readers but exclusive writers.
- **Notifications**: Pushes change events into a **Global Event Queue**. The **Editor** (dispatcher) then routes these to the appropriate **Per-Window/Document Queue** for processing.
- **State**:
    - `std::string filename`
    - `bool modified`
    - `int cursor_x, cursor_y` (logical character positions)
    - `int selection_start_x, selection_start_y`
    - `int selection_end_x, selection_end_y`
- **Methods**: High-level operations like `insert_char()`, `delete_line()`, `undo()`, `redo()`, `load_from_file()`, `save()`.
- **Multi-Cursor Editing**: Supports multiple simultaneous editing carets (`multi_cursors_`) with progressive selection (`Ctrl+D`), one-shot scope selection (`Ctrl+Shift+D` / `Ctrl+Shift+L`), synchronous typing, backspacing, deletion, and compound atomic undo.
- **External Modification Detection**: Tracks the file's modification time (`last_write_time`) on disk. The Editor periodically queries this value (rate-limited to once every 10 seconds by default, or configurable via the `TURBOSTAR_MTIME_CHECK_INTERVAL` environment variable). If a change is detected, a reload confirmation dialog is prompted. If declined, the timestamp is synchronized to the current disk mtime to prevent continuous prompting.

## Line class

The line class represents a single line of text.

- **Storage**: `std::string` (UTF-8 encoded).
- **Metadata**: A `std::vector<uint32_t>` or similar to store attribute data (colors, styles) for each character, computed asynchronously.
- **Methods**: `insert_at(pos, char)`, `split_at(pos)`, `merge(other_line)`.

## Conversation Data Model (agentlib/data/)

The active conversation state is represented as a structured C++ hierarchy to isolate logical data models from visual TUI layouts and support polymorphic turns:
- **`Turn`**: The base class for all conversation events, subclassed into concrete types:
  - `system_turn`: For system prompts and instructions.
  - `user_turn`: For user queries and input.
  - `model_response_turn`: For LLM output and tool calls.
  - `tool_execution_turn`: For results of tool executions.
  - `error_turn`: For transient processing errors.
- **`Transaction`**: A logical grouping of related turns (e.g., a user prompt and the corresponding assistant response and tool outputs).
- **`Episode`**: A chronological sequence of transactions representing a distinct session/boundary of the conversation, allowing independent compaction levels and summary generation.
- **`Conversation`**: The top-level coordinator managing the active episode and paged-out historical episodes.
  - *Episode Invariant*: All episodes except the "most current" active episode must be in the finalized state. When a new episode is created or added, all previous episodes are set to finalized and their state becomes final (meaning they will never be written to disk again; subsequent page-out transitions only clear their in-memory details to save RAM).

## Window class

The window class is a "View" that renders a portion of a `Document`.

- **Turbo Pascal Style**: Supports multiple windows on screen (tiled or stacked). A window can be "active" or "inactive".
- **Viewport**: Tracks the `top_line` and `left_column` currently visible.
- **Interaction**: Handles coordinate translation between screen space and document space.
- **Event Handling**: Processes events from its own **Per-Window Event Queue**, which are dispatched by the central `Editor`.
- **Linked Windows**: Windows can be mutually linked together (e.g., the Debugger and Run Output windows). Symmetrical linking/unlinking is handled in the base class via raw pointer tracking and automatic cleanup in `~window()`. If a window gets focus (active priority `9999`), any of its linked windows automatically have their priority elevated to `9998`, placing them directly below the active window in Z-order. This ensures linked windows are always rendered and sorted together above other background/MDI document windows.

## Event Queue and Logging

- **`event_queue`**: A thread-safe queue holding `editor_event` objects (e.g., `key_press`, `quit`). Used to safely pass events between threads or into the main dispatcher.
- **`event_logger`**: A singleton class responsible for capturing internal application state and writing it to a file or displaying it via the `--debug` flag.

## Editor class (Manager)

The central controller that manages the overall application state.

- **Documents**: Owns a list of all open `Document` objects.
- **Windows**: Manages the layout and lifecycle of `Window` objects on the screen.
- **UI Components**: Owns and coordinates the rendering of the `menu_bar` and `status_bar`.
- **Input Loop**: Drives the primary thread using a non-blocking `getch` loop (`timeout(50)`). It reads keybindings, wraps them into `editor_event` objects, pushes them into the **Global Event Queue**, and tracks interactive event response latency metrics (printing statistics like average, max, and threshold distributions upon exit).
## Focus Management and Event Routing

To ensure predictable interaction, Turbostar uses a focused-based event routing model:

- **Active Focus**: A global property (managed by the `editor` class) tracks which UI component currently has "Focus" (e.g., the `menu_bar`, an active `window`, or a `dialog`).
- **Routing Logic**:
    1.  The Input Loop pushes raw keys into the **Global Event Queue**.
    2.  The **Central Dispatcher** pulls from the Global Queue and identifies the current focus.
    3.  If the focus is the `menu_bar`, keys are routed to it.
    4.  If the focus is a `window`, keys are pushed into that specific **Per-Window Event Queue**.
- **Focus Transitions**:
    - Focus changes (e.g., pressing `Alt` to enter the menu or clicking a window) are handled through a centralized method (e.g., `editor::set_focus()`).
    - **Diagnostics**: Every focus change is logged with the source and destination component names for debugging.

## UI Elements and Structure

Based on the Turbo Pascal interface, Turbostar implements the following core UI components:

- **Desktop Layout**:
  - **`menu_bar`**: A class responsible for rendering the horizontal strip containing dropdown menus (File, Edit, Search, etc.) at the top.
  - **`status_bar`**: A class handling the horizontal strip at the bottom showing contextual hotkeys (e.g., `F1 Help`) and diagnostic `--debug` messages.
  - **Workspace**: The central area where editor windows and dialogs are rendered over a dark blue background.
- **Editor Window**:
  - **Border**: Double-line box drawing characters.
  - **Header**: Contains the centered filename, a close box `[■]` on the left, and a window number/maximize indicator on the right.
  - **Scrollbars**: Vertical (right edge) and horizontal (bottom edge) indicators, utilizing cyan shaded characters.
  - **Footer**: Shows cursor position (e.g., `1:1`) in the bottom left corner of the border.
  - **Content Area**: The viewport into the text `Document`.
- **Dialog Box**:
  - **Border**: Double-line box drawing characters over a light gray background.
  - **Shadow**: A black block-character shadow offset to the right and bottom to simulate depth.
  - **Controls**: Interactive buttons with distinct background colors (e.g., Green for OK) and text labels.
  - **Focus Management (Flattened Global Focus List / Option A)**: The dialog dynamically flattens its control hierarchy into a list of focusable leaf elements. Tab and Shift-Tab traversal and programmatic focus updates (e.g., `set_focus_by_name`) are resolved globally by the dialog, which syncs the active focus state and parent-child focus links upwards to prevent event loss.
- **Status Message Priority System**:
  - To prevent multiple active status bar messages (LSP hover help, compiler/build errors, agent statuses, transient dialog notifications) from competing for screen space, the editor implements a priority-based status message manager.
  - Message sources are mapped to distinct, defined priorities in `status_priorities`:
    - `HOVER` (Priority 10): Local hover help (e.g. LSP symbol hovers).
    - `INFO` (Priority 20): General transient info (e.g. "Saved conversation to...", "Agent: Thinking...").
    - `WARNING` (Priority 30): Compiler build errors or LSP diagnostic messages at the cursor.
    - `CRITICAL` (Priority 40): Fatal alerts or system warnings.
  - At render time, the status bar displays the active, non-expired message with the highest priority. When a higher priority message is cleared or expires, the display seamlessly falls back to the next highest active status message.

## Persistent Interactive Terminal Windows

Turbostar embeds full-featured interactive pseudo-terminal (PTY) shell windows directly within the editor layout:
- **Interactive Shell Session (`ui::terminal_window::start_shell`)**: Spawns the user's interactive shell (`$SHELL`, falling back to `/bin/bash` with `-i`) attached to a pseudo-terminal pair in the active file directory or project root. Environment variables (`TERM=xterm-256color`, `COLORTERM=truecolor`, `TURBOSTAR_TERMINAL=1`) ensure full color and ANSI support.
- **Tiled Window Integration (Option A)**: Terminal windows are first-class editor windows participating in window tiling, cascading, maximize/zoom (`F5`), switching via `Alt+1..9`, and display title bar widgets.
- **Key Routing & Pass-Through**: When focused, terminal windows enable `set_bypass_global_shortcuts(true)` to pass through standard terminal control characters (e.g., `Ctrl-C`, `Ctrl-L`, `Ctrl-D`, `Tab`) directly to the shell PTY while keeping function keys (`F1`-`F9`), `Alt`-hotkeys, and `Ctrl-G` input capture escape active.
- **Smart F6 Toggling & Cycling**: Pressing `F6` automatically opens a new terminal window if none are currently open. If terminal windows exist, `F6` focuses the terminal, cycles between open terminals, and returns to the active editor buffer.
- **Process Lifecycle & Auto-Close on Exit**: When the shell process terminates (via `exit` or EOF `^D`), `editor::update_terminal_windows()` detects `!is_alive()` and dispatches `event_type::close_window` targeted specifically at that window ID, cleanly closing the tiled window and returning focus to the editor.

## Git Gutter Indicators & Right-Margin Ghost Line Numbers

To provide contextual diff and line navigation awareness without breaking Turbostar's classic Borland-style full-width text canvas or wasting columns on a dedicated line gutter:

- **Left Window Border Indicators**:
  - Leverages the single-cell left window border (`x_`) to display color-coded unified diff status:
    - `║` Bright Green (Color Pair 30) for added lines.
    - `║` Bright Yellow (Color Pair 21) for modified lines.
    - `▸` Bright Red (Color Pair 31) for deletions above a line (including EOF deletions).
  - Left border markers provide indestructible 100% visibility regardless of line length or horizontal scroll.
  - Performance profiling heatmap indicators take visual precedence over diff markers on active hotspot lines (`pct >= 1.0`).

- **Right-Margin Ghost Line Numbers**:
  - Rendered right-aligned in idle margin whitespace along the right edge of each window in subtle, low-contrast cyan (`COLOR_PAIR(4) | A_DIM`).
  - Ghost line numbers remain pinned to the right window decoration during horizontal panning (`left_column_ > 0`).
  - **Z-Ordering & Natural Occlusion**: Ghost line numbers are drawn immediately after background fill and before document text. Long lines of code naturally overwrite the digits without requiring clipping, layout columns, or horizontal reflow.

- **Modal Line Number Navigation (`gutter_number_mode`)**:
  - **`Git Mode` (`[#Git]`)**: Displays Git HEAD line numbers. Deleted lines appear as number gaps (e.g., jumping from 44 to 47), and added lines display subtle green dots (`··`).
  - **`File Mode` (`[#File]`)**: Displays physical buffer line numbers (`1, 2, 3...`) matching compiler diagnostics. Automatically engaged upon compiler error navigation (`F4` / Next Error).
  - **`Off Mode` (`[#Off]`)**: Suppresses right-margin numbers while preserving left-border diff markers.
  - **Interactive Controls**: Clickable header widget `[#Git]` / `[#File]` / `[#Off]`, Window popup menu (`Alt+=`), and Git menu item (`Alt+G N`).

- **Live In-Memory Diffing**: To deliver instant real-time feedback as the user types without saving to disk, `git_manager` asynchronously caches HEAD file contents (`git show HEAD:./<file>`). When a document is modified in memory, `window::get_effective_diff()` invokes `git_manager::compute_diff()` via `<dtl/dtl.hpp>` to calculate unified hunks directly between cached HEAD lines and active buffer lines. Results are memoized against the document's monotonic revision counter (`revision_`), delivering sub-millisecond updates without spawning background processes or writing temporary files.

## Testing and Diagnostics

To ensure reliability, Turbostar incorporates testing and diagnostic infrastructure from the outset:

- **Unit Testing**: Basic unit tests for individual classes and methods.
- **End-to-End (E2E) Testing**: Uses tools like `tmux` or equivalent Python libraries to drive the application, send keystrokes, and verify the rendered output.
- **Event Logging Infrastructure**: A centralized event logging system to record application state changes and actions.
- **Command Line Options**:
  - `--log <filename>`: Writes the complete event log to the specified file upon application exit, enabling test cases to verify internal behavior.
  - `--debug [optional_string]`: Modifies the UI to show the most recent event log message in the bottom Status Bar. The message is wrapped in clear markers (e.g., `»message«` or `>>message<<`) to make it easily extractable by the E2E test framework. If `optional_string` is provided, the Status Bar only displays the most recent event message that contains the given string as a substring.

## Preferred Testing Patterns

For end-to-end (E2E) tests that involve complex content manipulation (e.g., block operations, formatting, search and replace), the preferred verification pattern is:

1.  **`assert_content_is(reference_file)`**: This primitive triggers a "Save As" operation within the editor to a temporary file and compares the resulting disk content against a "golden" reference file in `tests/data/`.
2.  **Why**: Comparing whole-file content on disk is significantly more robust than screen-scraping, as it is immune to scrolling issues, viewport limitations, and TUI rendering artifacts.
3.  **Diff Diagnostics**: If a mismatch occurs, the testing framework automatically prints a `diff -u` style output to the console to facilitate rapid debugging.

## LLM Agent Security & Validation

All tools exposed to the LLM agent must strictly adhere to the following architecture to prevent malicious or accidental disruption:

- **Two-Stage Validation:**
  - **Stage 1 (Automated Schema & Pre-invocation):** The central `tool_validator::validate_args` base method automatically performs a "test parse" against the JSON schema defined by `get_parameters_schema()`. It strictly enforces that all `required` properties are present and that their data types match exactly (e.g., rejecting strings passed as integers). The entire parsing loop is wrapped in a `try...catch` block to gracefully capture exceptions thrown by the JSON engine and return the exact rejection string to the LLM. Following schema validation, the tool implementation performs path validation against the `file_security_manager`.
  - **Stage 2 (Runtime Contextual):** Validations that require access to the instantiated tool state or dynamic runtime context (e.g., verifying a file exists before modifying it).
- **Hard Rule:** Raw LLM-provided JSON payloads must NEVER be passed directly to execution logic or external processes without first passing the centralized schema and type validations.
- **Sandboxed Execution:** External command tools (like compile scripts or arbitrary python scripts) must use the `command_runner` profiles which inherently map to isolated `systemd-run` environments.

## Non-mutating History Mode (openai_response)
To support model providers that do not allow mutating server-side assistant history or rewrites (such as certain AWS Bedrock models), Turbostar supports the `openai_response` API format.
- **is_mutation_possible() Check**: The `ai_agent` exposes a boolean `is_mutation_possible()` method which returns `false` if the model's format is `openai_response` (and `true` for standard stateless completion/Gemini APIs).
- **Compaction & Paging Disallowed**: History compaction (automatic and tools like `agent_compress_history`, `agent_restore_context`) is disabled. The corresponding tools are not advertised/injected into the payload and their runtime validations fail.
- **Synchronous Execution Fallback**: Asynchronous tool executions that rely on history mutation to rewrite previous tool call slots (such as `fs_compile_file`, `fs_compile_project`, and `run_shell_command`) are automatically overridden to run synchronously in this mode.
- **Stateful Turn Chaining (previous_response_id)**: In this mode, Turbostar tracks the server-returned `response_id` from both streaming chunk updates and sync JSON bodies, saving it as `last_response_id_` on the `ai_agent`. For subsequent turns, instead of sending the full conversation history, the formatter slices the conversation to only include messages/outputs created after the last assistant turn, and links them back to the server state by passing the `previous_response_id` parameter. Any history mutations or reloads automatically clear the tracked response ID to force a full history resend.
- **Stateful Responses Compaction**: When context bounds are approached or exceeded in stateful mode, the system automatically triggers compaction via a POST to `/v1/responses/compact`. This compacts the server-side response chain, returns a new response ID, updates `last_response_id_`, and marks compacted messages as `episode_level = 99` so they don't count towards local token limits.
- **Manual Compaction Command (/compact)**: Users can manually force stateful response compaction at any time by typing the `/compact` slash command in the agent input box, which invokes `ai_agent::force_compaction()`.
- **YOLO Mode Command (/yolo)**: Users can dynamically toggle or explicitly set (`/yolo on`, `/yolo off`) YOLO mode at any time. When enabled, permission prompts (shell execution, git push, web fetch, image import) and `ask_user` queries are automatically approved without opening interactive modal dialogs.
- **Agent Slash Commands (`command_registry`)**: The interactive agent window routes input lines starting with `/` to `command_registry` commands instead of submitting them to the model prompt. Registered commands include `/help`, `/info`, `/quit`, `/save`, `/stats`, `/memory`, `/episode`, `/pageout`, `/pagein`, `/model`, `/mcp`, `/skills`, `/compact`, `/rescan`, `/sysprompt`, `/clear`, and `/yolo`.

## Model Servers
To consolidate connection credentials and protocol settings, Turbostar supports defining "Model Servers" (configured via Options -> Model Servers... and serialized to `servers.json`). A model server stores:
- **ID & Name**: Unique identifier and human-readable label.
- **URL & API Key**: The endpoint address and auth token for the server.
- **API Format**: The protocol type (OpenAI, Response, Gemini, Copilot) used to communicate with the server.
In subsequent phases, models will map directly to these servers to eliminate duplicate configuration properties.

## Tool Families

To support modular and context-efficient tool management, tools are organized into namespaces called **Tool Families**.
- **The `"base"` Family**: All built-in tools default to the `"base"` family (via `tool_validator::get_family()`). This family contains core file editing and navigation tools that are always enabled and present in the agent's context.
- **MCP Tool Families**: MCP tools dynamically belong to their parent MCP server's name family (e.g., family name `server_name_`). This treats each MCP server as its own tool family by definition, allowing servers to be enabled/disabled as a unit.
- **Persistence**: Configuration settings for enabling/disabling tool families are stored in `config.ini` files using the `family.<family_name>.enabled` key, separating global (system) and project-local configurations.
- **Multi-Family Tools**: A tool can register under multiple families by returning a `|`-separated string from `tool_validator::get_family()` (e.g., `"binary|hexedit"`). The tool is available if *any* of its registered families are active.

## Data Type Map & Opportunistic Type Cache
To provide agents with high-value architectural awareness without token bloat or blocking tool latency:
- **Opportunistic Pre-Warming**: When files or companion headers are parsed for codemaps, discovered `Class`, `Struct`, and `Enum` symbols are automatically registered into `type_definition_cache`.
- **Zero-Latency In-Flight Stubs**: `fs_read_lines` scans read line buffers using `type_token_extractor` to identify candidate types, immediately registering `pending` stubs and queuing asynchronous LSP definition queries in a detached background worker.
- **Deduplication & Throttling**: Disclosed definitions are tracked per agent session in `ctx.reported_type_definitions` and capped at <= 3 definitions per tool call, excluding types defined in the read file itself or its companion header.
- **Cache Invalidation**: Mutations via `fs_replace_lines` or `fs_replace_content` immediately invalidate cached definitions originating from the modified file.

## Persistent Codemap Cache (`codemap_cache`)
To eliminate multi-second cold-start parsing delays across editor and MCP server restarts:
- **On-Disk Persistence**: Structured `codemap_symbol_info` trees are persisted to `~/.cache/turbostar/projects/<hash>/codemap_cache.json` using atomic temporary file creation and replacement.
- **Universal Format Coverage**: Caches symbols from all extraction paths—`clangd`, `pylsp`, `semcode`, regex fallback heuristics (`fallback_find_symbols`), and Markdown headings (`parse_markdown_headings`).
- **Smart Startup Validation & Eager Refresh**: On startup loading, file modification times (`mtime`) and file sizes are validated against disk via fast `stat()` checks:
  - **Fresh Entries**: Kept in memory (<0.1ms access) and eagerly register classes, structs, and enums into `type_definition_cache`.
  - **Stale/Modified Entries**: Immediately trigger asynchronous non-blocking prefetching (`prefetch_document_codemap_symbols`) at startup so language server ASTs are warming before the first user or agent toolcall.
  - **Deleted Files**: Automatically pruned from the cache.
- **LRU Eviction**: Tracks `last_used_ms` timestamps on all reads and writes; automatically prunes oldest entries when entry counts exceed limits.
- **Dirty Buffer Bypass**: Bypasses the cache when a file has unsaved in-memory modifications in `document_provider`.
- **Preemptive Precaching on File Edits**: File modifications (via tools like `fs_replace_content`, `fs_write_file`, `git_restore`, or interactive editor save) invoke `project_manager::lsp_save_document`. This emits `textDocument/didChange` and `textDocument/didSave` to the language server, invalidates stale symbol/type caches, and immediately kicks off asynchronous `prefetch_document_codemap_symbols`, ensuring symbol and codemap caches are pre-warmed for subsequent agent toolcalls.
- **Regex and Heuristic Fallbacks (`codemap_fallbacks`)**: Modularized in `src/codemap_fallbacks.{h,cpp}`, providing instantaneous (<1ms) heuristic scanning for source files (C, C++, Python, SystemVerilog) and structured Markdown heading outlines. Fallbacks operate strictly at the codemap layer when LSP backends are unconfigured, unindexed, timed out, or return empty symbol trees, avoiding process overhead while ensuring consistent symbol navigation across all file types. Language-specific heuristic analysis routines are centralized under `src/languages/<lang>/` (such as `src/languages/cpp/lexer.{h,cpp}` for error-resilient zero-copy tokenization, `symbol_extractor.{h,cpp}`, `type_token_extractor.{h,cpp}`, `call_token_extractor.{h,cpp}`, `include_extractor.{h,cpp}`, `highlighter.{h,cpp}`, and `keywords.{h,cpp}` for unified keyword/type classification).

## Profiling and Debugging
- **Memory Profiling**: To diagnose Out of Memory (OOM) errors, identify allocation bottlenecks, or track down memory leaks, use `heaptrack`. A detailed guide for agents is available at `docs/heaptrack.md`.
- **Process Execution & Interactive Debugging Registry**: Maintained in `tool_context::run_registry` and scoped to each agent session, tracking child processes and interactive GDB sessions started via `run_executable` or `agent_debug_coredump`.
  - **Linked App & Debugger Sessions**: When an executable is launched with `debugger: true`, the application run ID and GDB run ID are linked bidirectionally. Terminating either session via `agent_terminate_run` terminates both windows, stops both processes, and reports both IDs in the response message. Subsequent termination calls for the companion ID succeed benignly.
  - **Contextual Defaults & Aliases**: Tools operating on execution sessions (`agent_write_to_run`, `agent_terminate_run`, `agent_wait_for_app`, `agent_get_run_terminaldump`) accept integer run IDs, string representations (e.g. `"run_1010"`, `"1010"`), or symbolic aliases (`"gdb"`, `"app"`, `"latest"`). Omitting `run_id` automatically defaults to the most relevant active session (the active debugger session for inspection/PTY input, or the active application for termination/waiting).

## Plugins
Turbostar supports dynamic plugin loading to extend the editor's capabilities at runtime.
For details on dynamic loading architecture, C-linkage entry points, dynamic symbol linkage, tool registration lifecycles, and unit test lifecycle initialization, refer to `docs/design-plugin.md`.

## Project Templates
Turbostar provides a built-in project creation wizard (`ui_dialog_project`) and template engine (`project_template_manager`).
- **Template Embedding**: Template files in `templates/` are embedded at build time into `src/project_templates_embedded.h` via `scripts/embed_templates.py`. This ensures TurboStar can instantiate new projects without runtime dependency on external filesystem assets.
- **Available Templates**:
  - `meson_cpp`: C++ application with Meson build system (supports C++23, C++20, C++17).
  - `meson_c`: C application with Meson build system (supports C17, C11, C99).
  - `cmake_cpp`: C++ application with CMake build system.
  - `cmake_c`: C application with CMake build system.
  - `python_basic`: Python application with `pyproject.toml`.
  - `cargo_rust`: Rust application with Cargo.
  - `verilator_sv`: SystemVerilog simulation and linting project with Verilator and Meson (`meson compile -C build`, `meson test -C build`).
  - `fpga_ice40`: SystemVerilog FPGA hardware synthesis and implementation project targeting the Lattice iCE40-HX8K (Alchitry Cu) with Meson, Yosys, nextpnr-ice40, Project IceStorm (`icepack`), and `iceprog` with `pins.pcf` physical pin constraints.
- **Variable Substitution & Overrides**: The template manager performs token expansion (`@@PROJECT_NAME@@`, `@@PROJECT_NAME_LOWER@@`, `@@LANGUAGE_STD@@`) and resolves standard-specific file overrides (e.g., `.C++17`).

## Language Server Protocol (LSP) Architecture

Turbostar integrates Language Server Protocol (LSP) intelligence across editor interactions (F3 definition jumps, F4 references, hovers, diagnostics) and LLM agent tools (`fs_read_symbol`, `code_get_definition`, `code_get_references`, `code_get_scope`, and `fs_file_codemap`).

The LSP subsystem uses the **Strategy Pattern** to separate protocol abstraction from concrete language server backends:
- **`lsp_backend` Interface** (`src/lsp_backend.h`): Abstract base class specifying virtual operations for document synchronization (`open_document`, `update_document`, `save_document`), interactive queries (`request_hover`, `request_document_highlight`, `request_selection_range`), symbol inspection (`query_definition`, `query_references`, `query_workspace_symbols`, `query_document_symbols`, `prefetch_document_symbols`), call & type hierarchies, and diagnostics.
- **`standard_lsp_backend` Implementation** (`src/standard_lsp_backend.h`, `src/standard_lsp_backend.cpp`): Subclass managing external language server processes (`clangd` for C/C++, `pylsp` for Python) spawned with lowered CPU priority (`nice -n 10`), stdio JSON-RPC streams, versioned document tracking, symbol caching, deduplicated asynchronous document symbol prefetching (`prefetch_document_symbols` with `in_flight_symbol_queries_`), full save synchronization (`didChange` + `didSave`), and parallel server teardown.
- **`lsp_manager` Facade** (`src/lsp_manager.h`, `src/lsp_manager.cpp`): High-level facade owned by `project_manager`. Exposes a stable interface to the editor, codemap helpers, and agent tools while forwarding requests to the active `lsp_backend` strategy.
- **Parallel Document Symbol Prefetching (`prefetch_document_codemap_symbols`)**: When batch search or inspection tools (such as `fs_grep_files`) traverse files, candidate matches immediately trigger non-blocking asynchronous document symbol prefetching (`codemap_utils::prefetch_document_codemap_symbols`) across discovered files. This leverages the language server's concurrent JSON-RPC queue to warm `symbol_cache_` in the background during directory traversal, eliminating sequential multi-second query timeouts when later formatting enclosing scope annotations and codemaps. File edit tools also trigger this prefetch preemptively upon save.
- **`semcode_backend` Implementation** (`src/semcode_backend.h`, `src/semcode_backend.cpp`): Specialized backend inheriting from `standard_lsp_backend`. Automatically activated on demand when a `.semcode.db` database is detected in the project root and `semcode-lsp` is available. Employs a hybrid architecture:
  - **LSP Protocol (`semcode-lsp`)**: Handles fast stdio JSON-RPC queries for `textDocument/definition` (go to definition) and `textDocument/references` (caller lookup).
  - **SQLite Indexer Integration (`semcode_indexer`)**: Replaces external CLI subprocess execution and stdout scraping with embedded SQLite queries (`functions` and `types` tables indexed with B-trees). Resolves outgoing call hierarchies, definitions, type inspections, and caller references in microseconds (<1ms) directly in-process. Filters out ambiguous definitions (e.g. ubiquitous macros and multi-architecture definitions like `memset` or `BIT`) and cross-architecture targets (`arch/`) directly within `semcode_backend` queries with strict uniqueness checks.
  - **Zero-Block Startup & Git HEAD Auto-Reindexing**: When starting up or checking out a new git commit, `semcode_backend` immediately opens the newest available SQLite index as a fallback so code navigation and tool queries never block. If the current git HEAD commit does not have a matching index (`semcode_<git_head>.db`), it spawns a background worker thread (`semcode_bld`) to build and index the new HEAD asynchronously, atomically hot-swapping to it upon completion.
  - **Optimized Document Synchronization**: Leverages semcode's native working directory overlay to inspect uncommitted changes directly without streaming full file bodies over stdio.
  - **Asynchronous Hover Worker**: Offloads hover queries to a dedicated background worker thread (`hover_worker_loop`), returning immediately (<1ms) to keep the editor main UI loop non-blocking. Uses `hover_cache_` in `standard_lsp_backend` to memoize responses, providing instant zero-latency cache hits as queries resolve.
- **Outgoing Call Resolution & Context-Aware Disambiguation (`call_resolver`, `call_token_extractor`)**: In backends that do not implement `prepareCallHierarchy` / `outgoingCalls` (such as `semcode-lsp`), or when call hierarchy misses in-slice calls, `codemap_utils::get_outgoing_calls_in_range` uses `call_resolver` to harvest and resolve callees. Candidates are extracted via `tools::call_token_extractor`, filtering keywords, operators, and definition headers. Outgoing calls whose positions map to enclosing function headers overlapping the requested range are preserved rather than dropped, while intra-file and enclosing self-definition headers are excluded from external called dependencies. Candidate definitions are disambiguated through caller context scoring (matching `#include` directives in the caller file, companion `.cpp` implementation scoring bonus, subsystem/directory proximity, and favoring generic headers over architecture-specific ports like `arch/`) while strictly isolating test directories (`tools/testing/`, `selftests/`, `tests/`) from non-test callers. Calls resolving to `.h` declarations are automatically upgraded to their companion `.cpp` implementation files when the symbol definition is present.
- **Semcode Streaming Indexer & Persistent SQLite Storage (`semcode_indexer`, `turbostar-semcode-indexer`)**:
  - Provides a standalone high-performance binary (`turbostar-semcode-indexer`) and C++ library class (`semcode_indexer`) to ingest massive `semcode` JSON dumps (`dump-functions`, `dump-types`) into a local SQLite database with zero full-DOM JSON allocation. Invocations automatically pass `--reindex-if-stale` so `semcode` transparently updates stale or older-schema databases in place.
  - Generates optimized B-tree indices covering function names, file ranges, and type names.
  - Achieves over 170,000 records/sec throughput: fully indexing 1.12 million Linux kernel functions (899MB) and 168k types (167MB) into a compact 256MB database in ~8.2 seconds.
  - Point lookups for function callees/types or type signatures execute in under 70 microseconds, eliminating subprocess latency during code navigation, codemaps, and LLM tool queries.
  - Databases are keyed by git commit SHA (`semcode_<commit_hash>.db`), allowing permanent cache reuse across editing sessions and code reviews for clean git trees.
  - **LRU Cache Pruning & Retention**: Automatically enforces a strict retention limit (`--max-dbs`, default: 2) via `semcode_indexer::prune_cache_directory()`, sorting databases by access/modification time and purging oldest databases along with companion journal/wal/shm files to prevent disk bloating. Opening a database automatically updates its modification time for LRU tracking.
  - **Git Blob Hash Filtering**: To eliminate stale historical function and type definitions exported by `semcode dump-*` scans across multi-commit databases, `semcode_indexer` queries git tree blob SHAs via `git ls-tree -r <ref>` (and hashes uncommitted working tree modifications via `git status --porcelain` and `git hash-object`). Ingestion streams filter each record's `git_file_hash` against this valid set in O(1) time, ensuring zero historical duplicates or stale code definitions are indexed.
- **Extensibility & Mocking**: Enables alternative backends and mock backends in unit tests to achieve high test coverage without requiring external server binaries.
