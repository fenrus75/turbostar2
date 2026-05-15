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
  - Synchronization: `std::shared_mutex` and a thread-safe **Event Queue**.
- Cursor mapping: Custom lightweight UTF-8 utility mapping logical character index <-> UTF-8 byte offset.


# Architecture

## Document class

The document class represents a whole file and serves as the primary "Model" in our MVC-like architecture.

- **Data Structure**: `std::vector<Line>` where each `Line` represents a single line of text.
- **Undo/Redo**: Implemented via a transaction log. Each transaction stores deltas at the `Line` level to balance implementation simplicity with memory efficiency.
- **Concurrency**: Each document will be protected by a `std::shared_mutex` (RW-mutex) to allow multiple readers but exclusive writers.
- **Notifications**: Pushes change events into an **Event Queue** to notify interested observers (like Windows).
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
- **Event Handling**: Listens to the `Document`'s event queue to trigger partial or full redraws.

## Editor class (Manager)

The central controller that manages the overall application state.

- **Documents**: Owns a list of all open `Document` objects.
- **Windows**: Manages the layout and lifecycle of `Window` objects on the screen, mimicking the Turbo Pascal environment.
- **Input Loop**: The primary thread that reads from `ncurses`, translates keybindings (using a keymap derived from `docs/joe-keys.md`), and dispatches commands to the active document/window.
- **Event Dispatcher**: Centralized mechanism to process events and coordinate updates.
- **Status Bar / Menu**: Manages the Turbo Pascal-style chrome (menus, status lines).