# Agent Subsystems Coordinator (`agent_subsystems`)

## Overview & Goals
`agent_subsystems` is a centralized lifecycle coordinator for all agent-side and plugin subsystems in TurboStar.

TurboStar is designed to be an ultra-responsive, lightweight text editor. When a user runs `turbostar file.txt` solely to edit text, they should not pay the performance penalty (heap allocations, dynamic library loading, child process spawning, JSON-RPC handshakes) of the AI agent infrastructure.

The primary goals of `agent_subsystems` are:
1. **Deferred Initialization**: Defer loading and initialization of heavy agent subsystems (dynamic plugins, MCP server management, subagents, skills, A2A configurations, and tool registries) until the first time an agent feature or relevant UI element is invoked.
2. **Idempotency & Thread Safety**: Ensure initialization happens exactly once across any number of calling threads via `std::call_once`.
3. **Single Source of Truth**: Replace dispersed, duplicated subsystem initialization sequences across `editor.cpp`, `editor_events_ui.cpp`, `main.cpp`, `agentcli`, etc. with a unified coordinator call.

## Subsystems Managed
- Dynamic plugins (`plugin_loader`)
- Global tool registry (`agentlib::tool_registry`)
- Command registry (`command_registry`)
- Skills and skill VFS (`agentlib::skill_manager`)
- Subagents (`agentlib::subagent_manager`)
- Agent-to-Agent communication (`a2a::a2a_server_manager`)
- MCP client process & tools (`agentlib::mcp_manager`)

## Constraints & Invariants
- `agent_subsystems::initialize()` must be completely safe to call repeatedly from any thread.
- If an editor session never opens an agent window, executes an agent action, or opens an agent-related dialog, zero agent processes should be spawned and zero agent plugins should be dynamically loaded into the process address space.
- Unit tests that explicitly test individual subsystems (e.g. testing `tool_registry` or `skill_manager` in isolation) can still initialize their respective components directly if needed.

## Lessons Learned
- Calling subsystem initializers indiscriminately inside general class constructors or lookup methods can cause unwanted side effects (such as unit tests scanning directories or loading stale host plugins). Having an explicit lifecycle coordinator called at UI and CLI entry points cleanly separates core editor workflows from agent features.
