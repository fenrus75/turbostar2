# Agent Run Registry (`agent_run_registry`)

## Goals
`agent_run_registry` provides per-agent-session tracking and resolution for running processes, applications, and attached interactive debuggers (GDB/GDBServer) launched via `run_executable` or `agent_debug_coredump`.

Its primary responsibilities are:
1. **Alias Resolution**: Translates symbolic strings (`"gdb"`, `"debugger"`, `"app"`, `"main"`, `"latest"`, `"run_1010"`) and integers into validated numerical run IDs.
2. **Contextual Defaults**: Allows tools like `agent_write_to_run` or `agent_terminate_run` to omit `run_id`, automatically routing commands to the active debugger (if one is attached) or the most recent application.
3. **Linked Teardown Coordination**: Tracks paired application and debugger windows (such as split-screen GDB sessions) so terminating the application cleanly reports and marks both IDs as terminated, preventing confusing error messages when the LLM attempts to clean up the second ID.

## Architecture & Constraints
- **Session Scope**: Owned by `ai_agent` and referenced via `tool_context::run_registry` (`std::shared_ptr<agent_run_registry>`). Child subagents or isolated test contexts receive independent registry instances.
- **Thread Safety**: All internal maps and history vectors are protected by `mutex_`. Locks are strictly short-lived and never held across PTY reads/writes or tool execution.
- **Benign Double-Termination**: When a run is marked terminated as part of a linked pair, `is_already_terminated()` allows `agent_terminate_run` to succeed benignly if the LLM issues a redundant termination call.

## Lessons Learned
- **LLM Cleanup Checklists & Double Termination**: When `run_executable(..., debugger=true)` returns `{app_run_id: 1010, gdb_run_id: 1011}`, LLMs instinctively issue `agent_terminate_run(1010)` followed by `agent_terminate_run(1011)`. Because the editor terminates linked terminal windows in tandem, the second call failed with "Run ID not found". Tracking linked runs in the registry and reporting both terminated IDs in the tool output eliminates redundant calls and prevents error spikes.
- **Case-Insensitive Symbolic Aliasing**: Supporting `"gdb"` and `"app"` reduces token overhead and prevents integer transposition bugs across multi-turn debugging sessions.
