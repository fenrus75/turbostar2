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
- Multi-threaded:
  - Main thread: UI rendering and input handling.
  - Background threads: Per-document worker threads for heavy tasks (like future syntax highlighting or file I/O).
  - Synchronization: `std::shared_mutex` and a thread-safe system of **Global** and **Per-Window/Document Event Queues**.
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
- **Central Dispatcher**: Immediately pulls events from the **Global Event Queue** and dispatches them to update the UI or target specific **Per-Window/Document Queues**.

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