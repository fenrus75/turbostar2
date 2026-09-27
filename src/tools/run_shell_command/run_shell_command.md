# run_shell_command & shell_command_resteer

## Overall Goals and Architecture
`run_shell_command` allows agents to run arbitrary shell commands when native tools do not suffice. Because running arbitrary shell commands can interrupt the user with security approval dialogs, introduce shell injection risks, and bypass project sandboxing, the tool incorporates:
1. **Security Sandboxing**: Execution is constrained to safe paths with ANSI escape filtering.
2. **Re-steering Engine (`shell_command_resteer`)**: Inspects shell commands before execution and intercepts common commands that duplicate native tools (e.g. `grep`, `sed`, `git`, `meson test`, `gdb`), denying the shell command and providing the exact native tool call to encourage frictionless, structured tool usage.
3. **Bypass via `force: true`**: When native tools are genuinely insufficient, agents can pass `force: true` to bypass re-steering and request explicit user confirmation.

## Constraints
- Must avoid false positives in regex matching while handling path prefixes (e.g. `cd <dir> &&`).
- Must handle quotes and spaces when parsing commands and arguments.

## Lessons Learned
- **Re-steering Interactive Tools**: Agents frequently attempt batch `gdb` shell commands (e.g. `gdb -batch -ex "run" ...`) when diagnosing test failures or crashes. Intercepting `gdb` and recommending `run_executable(binary="build/<target>", debugger=true)` with `agent_write_to_run` eliminates user permission prompts and provides live, multi-turn interactive debugging.
