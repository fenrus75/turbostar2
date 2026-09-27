# run_executable

## Overall Goals and Architecture
`run_executable` allows LLM agents and the TurboStar environment to execute binaries, benchmarks, and unit tests directly within the project sandbox without requiring interactive user confirmation prompts.

Key features:
1. **Direct Execution**: Executes binaries relative to the project directory or `build/` folder without shell escaping vulnerabilities or approval friction.
2. **Interactive Debugging (`debugger: true`)**: Spawns GDB/GDBServer in a split-screen session, returning `app_run_id` and `gdb_run_id`. Agents interact with GDB using `agent_write_to_run` to set breakpoints, inspect live memory/stacks, and step through code before terminating with `agent_terminate_run`.
3. **Performance Profiling (`collect_performance: true`)**: Injects profiling sampling via `LD_PRELOAD` to collect CPU cycle distributions for `agent_get_profile_summary` and `agent_get_profile_details`.
4. **Crash Interception & Notification**: Intercepts crashes, captures signals and failed assertions, and registers crash entries for post-mortem analysis via `crashdump_get_info` and `agent_debug_coredump`.

## Constraints
- Must enforce project boundaries so agents cannot run arbitrary system binaries outside allowed project paths.
- Proper cleanup of spawned PTYs and child processes via `agent_terminate_run`.

## Lessons Learned
- **Workflow Clarity in Tool Description**: Agents naturally default to shell `gdb -batch` unless the tool description explicitly mentions companion tools (`agent_write_to_run` with `output: true` and `agent_terminate_run`), because LLMs need a clear multi-tool recipe to orchestrate interactive sessions.
