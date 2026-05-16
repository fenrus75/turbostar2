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
| `docs/keybindings.md`  | Key binding documentation  |
| `docs/colorscheme.md`  | Color scheme documentation |
| `docs/buttonrecipe.md` | How to make UI buttons    |

# Git policy

After making a change, create a git commit for the change.

# Technical decisions

- Turbostar uses C++, the C++23 language version
- Turbostar uses the meson build system
- Each class gets its own .cpp file with a matching header file
- Data classes and presentation classes should be strictly separated
- ncursesw is used for creating the TUI
- RAII memory management paradigm throughout
- Design goal: responsiveness 
- Multi-tier Locking Model:
    - **Document-level**: Each `document` uses a `std::shared_mutex` to protect the structure of the document (the list of lines) and the cursor/selection state. `shared_lock` is used for rendering and reading state, while `unique_lock` is used for structural changes or moving the cursor.
    - **Line-level**: Each `line` uses its own `std::shared_mutex` to protect its internal UTF-8 string data. This allows background threads to process different lines of the same document concurrently without blocking the main document or each other.
    - **Lock Ordering**: To prevent deadlocks, the locking order is always **Document -> Line**.
    - **Snapshots**: `line::get_text()` returns a `std::string` copy to provide a thread-safe snapshot of the line content to readers.
  - Per-Window/Document Event Queues: Thread-safe queues for passing events between threads.
- **Selection Model**:
  - Turbostar uses a **Persistent Marker Selection** model (WordStar/Joe style).
  - Two markers, `Selection Begin` (^KB) and `Selection End` (^KK), define a contiguous range of text.
  - The selection is "persistent": it stays active even as the cursor moves, until specifically moved or hidden.
  - **Editing Awareness**: The document class ensures that markers stay pinned to the logical text. If characters are inserted or deleted before a marker, the marker's position is adjusted.
  - **Visuals**: The range between Begin and End markers is highlighted using a distinct color pair (Inverse).
- **Stateful Key Sequences**:
  - Turbostar supports "prefix" keys, most notably the `Ctrl-K` block.
  - When a prefix key is pressed, the editor enters a sub-state waiting for the next "command" key.
  - This enables a large number of commands while keeping the primary keymap uncluttered.
- **Visual Colors & Highlighting**:
  - Turbostar uses a 16-color palette (base colors + 8 for high intensity).
  - Explicit color pairs MUST be used for all UI elements and highlights.
  - **Avoid `A_REVERSE`**: Never use the `A_REVERSE` attribute to highlight focus or selection. Instead, always allocate and use a dedicated color pair (e.g., Black on Green). This ensures visual consistency and predictability across different terminal environments.
- Cursor mapping: Custom lightweight UTF-8 utility mapping logical character index <-> UTF-8 byte offset.


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

## Line class

The line class represents a single line of text.

- **Storage**: `std::string` (UTF-8 encoded).
- **Metadata**: A `std::vector<uint32_t>` or similar to store attribute data (colors, styles) for each character, computed asynchronously.
- **Methods**: `insert_at(pos, char)`, `split_at(pos)`, `merge(other_line)`.

## Window class

The window class is a "View" that renders a portion of a `Document`.

- **Turbo Pascal Style**: Supports multiple windows on screen (tiled or stacked). A window can be "active" or "inactive".
- **Viewport**: Tracks the `top_line` and `left_column` currently visible.
- **Interaction**: Handles coordinate translation between screen space and document space.
- **Event Handling**: Processes events from its own **Per-Window Event Queue**, which are dispatched by the central `Editor`.

## Event Queue and Logging

- **`event_queue`**: A thread-safe queue holding `editor_event` objects (e.g., `key_press`, `quit`). Used to safely pass events between threads or into the main dispatcher.
- **`event_logger`**: A singleton class responsible for capturing internal application state and writing it to a file or displaying it via the `--debug` flag.

## Editor class (Manager)

The central controller that manages the overall application state.

- **Documents**: Owns a list of all open `Document` objects.
- **Windows**: Manages the layout and lifecycle of `Window` objects on the screen.
- **UI Components**: Owns and coordinates the rendering of the `menu_bar` and `status_bar`.
- **Input Loop**: Drives the primary thread using a non-blocking `getch` loop (`timeout(50)`). It reads keybindings, wraps them into `editor_event` objects, and pushes them into the **Global Event Queue**.
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

## Testing and Diagnostics

To ensure reliability, Turbostar incorporates testing and diagnostic infrastructure from the outset:

- **Unit Testing**: Basic unit tests for individual classes and methods.
- **End-to-End (E2E) Testing**: Uses tools like `tmux` or equivalent Python libraries to drive the application, send keystrokes, and verify the rendered output.
- **Event Logging Infrastructure**: A centralized event logging system to record application state changes and actions.
- **Command Line Options**:
  - `--log <filename>`: Writes the complete event log to the specified file upon application exit, enabling test cases to verify internal behavior.
  - `--debug [optional_string]`: Modifies the UI to show the most recent event log message in the bottom Status Bar. The message is wrapped in clear markers (e.g., `»message«` or `>>message<<`) to make it easily extractable by the E2E test framework. If `optional_string` is provided, the Status Bar only displays the most recent event message that contains the given string as a substring.