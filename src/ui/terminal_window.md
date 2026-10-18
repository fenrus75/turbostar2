# Terminal Window (`ui::terminal_window`)

## Goals
The `terminal_window` class provides an interactive terminal emulator window embedded within Turbostar. Its core responsibilities include:
1. **Interactive Subprocess & PTY Hosting**: Managing master/slave pseudo-terminal (PTY) pairs, executing interactive commands or sandboxed child processes (e.g. application runs, build tools, test runners, GDB debuggers), and forwarding bidirectional I/O.
2. **ANSI Terminal Emulation**: Integrating with `ansi_terminal_emulator` to parse ANSI escape sequences, colors, cursor positions, scrolling regions, and screen buffers for full-screen curses rendering.
3. **Debugger Session Integration**: Hosting interactive GDB debugger sessions with on-screen debug control buttons (`[Break]`, `[Step]`, `[Next]`, `[Cont]`, `[Quit]`) on the bottom border, mouse click forwarding to GDB commands, and coordinated layout splitting with application output windows.
4. **Input Capture & Pass-Through**: Managing keyboard focus modes (`capture_input_`), allowing direct keystroke forwarding to child processes while providing `^G` hotkey escape mechanisms to return focus to the editor.
5. **Output Recording & Parsing**: Providing session recording, sanitization, and structured log parsing via `build_log_parser` (e.g., `gcc_log_parser`).

## Architecture and Constraints
- **Subclass of `window`**: Inherits core window geometry, event queues, and rendering borders from `::window`.
- **Decoupled Debugger State**: Rather than inferring GDB semantics from window titles (such as `"Debugger (GDB)"`), `terminal_window` maintains an explicit `is_gdb_` flag accessible via `is_gdb()` and `set_gdb(bool)`.
- **PTY Buffer Safety & Thread Safety**: PTY reads occur on the UI loop via `update_pty()`, safely updating emulator state without cross-thread data races.
- **Process Teardown**: Automatically terminates subprocesses cleanly in `~terminal_window()` and `stop_process()` using SIGTERM followed by SIGKILL if necessary.

## Lessons Learned
- **Robust Debugger Identification via `is_gdb()` vs Window Titles**: Relying on string title comparisons (`tw->get_title() == "Debugger (GDB)"`) was fragile because window titles are presentation strings that vary between regular GDB sessions, coredump debuggers (`"Coredump Debugger (GDB)"`), or user/agent custom session names. Coupling window layout splitting, F6 window toggling, debug button rendering, and quit command signaling to an exact title string broke whenever titles changed. An explicit `is_gdb_` boolean flag and `is_gdb()` accessor reliably encapsulates debugger capabilities independent of the window's display title.
- **Subprocess Teardown & FIFO Cleanup**: Ensuring child processes are terminated with SIGTERM and slave PTY file descriptors closed before unlinking temporary FIFOs prevents blocked I/O and orphaned zombie processes.
