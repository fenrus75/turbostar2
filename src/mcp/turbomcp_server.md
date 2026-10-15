# TurboMCP Server (`turbomcp_server`)

## Goals
The `turbomcp_server` class (`src/mcp/turbomcp_server.h`, `src/mcp/turbomcp_server.cpp`) implements a headless Model Context Protocol (MCP) JSON-RPC 2.0 server over `stdin`/`stdout`. It exposes Turbostar's registered `tool_registry` tools to external IDE and CLI agent harnesses (such as Google Antigravity, GitHub Copilot, and Claude Code) while maintaining full access to Turbostar's workspace security sandbox, LSP integration, and persistent `tool_context`.

## Architecture and Constraints
- **Headless Editor Backing**:
  - Instantiates a headless `editor` and background event loop thread (`event_loop_worker`) to process asynchronous editor events and PTY terminal updates required by tools.
- **Tool Exposure Filtering**:
  - Only exposes tools where `tool_validator::expose_in_mcp()` returns `true`, hiding TUI-bound tools (such as `open_in_editor` or `perform_code_review`).
- **Parameter Naming Identities (`mcp_tool_identity`)**:
  - Supports three parameter naming identities when advertising `inputSchema` in `tools/list`:
    - `turbostar` (default): Linux kernel style `snake_case` (`start_line`, `target_content`).
    - `antigravity`: Google style `PascalCase` (`StartLine`, `TargetContent`).
    - `copilot`: Microsoft style `camelCase` (`startLine`, `targetContent`).
  - Automatically detects the active identity from `params.clientInfo.name` during the MCP `initialize` handshake unless explicitly overridden via `--mcp-identity` or `TURBOSTAR_MCP_IDENTITY`.
  - Rewrites parameter references inside both property-level and top-level tool `description` fields using `RE2` (`transform_description_for_identity`) restricted to the exact `(old_key, new_key)` pairs renamed for that tool (replacing multi-word `snake_case` tokens on word boundaries and single-word parameter tokens when quoted/backticked or in slash-separated parameter lists, avoiding false positives on normal English prose).
  - Inbound `tools/call` requests accept all parameter naming styles and aliases regardless of the advertised identity via `tool_validator::normalize_object_args`.

## Lessons Learned
- **Retaining `tool_context` Across Calls**: Preserving a single `mcp_context_` member across `tools/call` invocations is essential so stateful counters (like `edit_sequence_counter`) and active tool family registrations persist for the duration of the MCP session.
- **Advertising Host-Matched Parameter Casing & Descriptions**: Keeping canonical tool names (`fs_read_lines`, `fs_replace_content`, etc.) while transforming advertised parameter schemas and description text to match the host harness's native casing convention (`PascalCase` for Antigravity, `camelCase` for Copilot) reduces LLM friction without causing tool-name collisions or documentation drift.
