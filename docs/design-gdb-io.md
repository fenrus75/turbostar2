# Architecture of GDBServer Input/Output in Turbostar

## The Problem: The `tcsetattr` / `SIGTTOU` Infinite Loop
When Turbostar starts an application under the internal debugger, it splits the screen to show both the application's output ("Run Output" window) and the debugger ("Debugger (GDB)" window). 

The target application is launched inside a `terminal_window` via `gdbserver` using a command like:
```bash
exec gdbserver localhost:<port> ./build/exe
```

Under this architecture, the target application often gets permanently "stuck" inside `tcsetattr()` during initialization (e.g. at `__tcsetattr (fd=1, optional_actions=1, termios_p=...)`).

### Why this happens:
1. **Foreground Process Group Mismatch**: The `terminal_window` allocates a Pseudo-Terminal (PTY) and spawns `bash` to run `gdbserver`. `bash` and `gdbserver` act as the foreground process group. When `gdbserver` forks the target application, the target is often not the foreground process group of the PTY.
2. **The `SIGTTOU` Signal**: When a background process (or any process not in the foreground process group) attempts to modify terminal attributes via `tcsetattr()`, the Linux kernel generates a `SIGTTOU` signal directed at the calling process's process group.
3. **Ptrace Interception**: Because the target application is being debugged, `gdbserver` intercepts the `SIGTTOU` via `ptrace()` before it can be delivered to the application, and reports it to `gdb`.
4. **GDB's Handling Rules**: Turbostar configures `gdb` with `-ex "handle SIGTTOU nostop noprint nopass"`. This tells GDB to silently resume the application *without* delivering the signal (`nopass`).
5. **The Infinite Loop**: The application resumes the interrupted `tcsetattr` system call. The kernel again sees that the application is not in the foreground process group, and generates another `SIGTTOU`. GDB intercepts it, drops it, and resumes the application again. This results in a tight infinite loop where the application is completely stuck in the `tcsetattr` call, consuming 100% CPU on that thread but never making progress.

## The Architecture Solution: Signal Disposition Inheritance

To solve this, we must ensure the application's `tcsetattr` call is allowed to succeed without generating a `SIGTTOU`. 

According to POSIX semantics for `tcsetattr()`:
> *If the calling process is ignoring or blocking the SIGTTOU signal, the process shall not be sent a SIGTTOU signal, and the operation shall be performed normally.*

Because signal dispositions (`SIG_IGN`) are inherited across `fork()` and `exec()`, the architectural solution is to ignore `SIGTTOU` and `SIGTTIN` in the shell *before* we execute `gdbserver`.

### Implementation: The Wrapper Script
Instead of launching `gdbserver` directly, the `Run Output` terminal window should wrap the launch command to ignore the terminal job control signals.

```bash
# Correct architectural launch command:
trap '' SIGTTOU SIGTTIN; exec gdbserver localhost:<port> ./build/exe
```

**Flow of Execution:**
1. `terminal_window::start_process` runs `bash -c "trap '' SIGTTOU SIGTTIN; exec gdbserver ..."`.
2. `bash` sets the signal disposition of `SIGTTOU` and `SIGTTIN` to `SIG_IGN` (ignore).
3. `bash` execs `gdbserver`. `gdbserver` inherits the `SIG_IGN` dispositions.
4. `gdbserver` forks the target application. The target inherits the `SIG_IGN` dispositions.
5. The target application calls `tcsetattr()`. 
6. The kernel checks the signal disposition. Seeing that `SIGTTOU` is ignored, the kernel *bypasses* sending the signal and allows `tcsetattr()` to modify the PTY attributes directly.
7. The application initializes successfully and IO flows naturally to the "Run Output" window.

## GDB TTY / Standard IO mapping
For `gdb` to properly interact with the program, standard IO streams (stdin, stdout, stderr) must map cleanly to the allocated pseudo-terminal:
- **`app_tw` (Run Output Window)**: Acts as the primary terminal for the application. Its PTY master connects to the UI, while its PTY slave is inherited by the application as fd 0, 1, and 2.
- **`gdb_tw` (Debugger Window)**: Communicates exclusively via the GDB Machine Interface (or CLI) to control `gdbserver` remotely via TCP. It does not share terminal state with the target application, preventing IO interleaving between debugger commands and application output.

This strict separation ensures that the application behaves as if it is running in its own terminal, while the debugger controls it out-of-band over the `localhost` socket.
