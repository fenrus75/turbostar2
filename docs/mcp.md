# Model Context Protocol (MCP) Design

This document details the architecture, configuration, UI integration, and security model for adding Model Context Protocol (MCP) support to the Turbostar editor.

## 1. Architectural Model & Discovery

Turbostar will discover and categorize MCP servers into **System MCPs** (global/user level) and **Project MCPs** (specific to the active repository).

### 1.1. Discovery Paths
The discovery parser will scan the following config paths:
*   **System/Global Config Files**:
    *   `~/.claude/mcp.json`
    *   `~/.copilot/mcp-config.json`
    *   `~/.gemini/config/mcp_config.json`
*   **Project Config File**:
    *   `<PROJECT_ROOT>/.agents/mcp_config.json`

### 1.2. Configuration Formats
*   The external discovery configs are JSON objects matching the standard Claude Desktop configuration format:
    ```json
    {
      "mcpServers": {
        "server-name": {
          "command": "npx",
          "args": ["-y", "@modelcontextprotocol/server-everything"],
          "env": {}
        }
      }
    }
    ```
*   **Detection of MCP types**: Prepare for detecting types like `uv`, `python`, `npm`, or `other` to allow specialized setups.

### 1.3. Conflict Resolution
If an MCP server name conflict occurs (a server with the same name is defined in both global and project configurations), the following resolution rules apply:
*   The project-level definition normally overrides/shadows the global one.
*   *Exception/Twist*: If the global version of the server is explicitly enabled and the project-local one is disabled in the project configuration, the global one still wins and remains active.


---

## 2. Security & Sandboxing

Security is built into the core design of the MCP executor:

### 2.1. Trust Model
*   **System MCPs**: Assumed safe. On by default.
*   **Project MCPs**: Assumed untrusted. Off by default.
    *   *Permission Flow*: Prompt the user once when the Project MCP is first enabled to approve starting the server and establishing its sandbox permissions (e.g. read-only, read-write, extra paths). Once approved, permissions are enforced silently.
*   **Metadata/Discovery Action**: Asking the MCP for supported tools (initial `tools/list` handshake) is always performed within a strictly restricted **read-only sandbox**.

### 2.2. Sandbox Execution
*   MCP servers will execute as subprocesses using the editor's existing `sandboxed_command_runner` (utilizing `systemd-run --user`).
*   Project MCPs will have strict resource limits, network constraints (disabled by default unless configured), and no access to the home directory (using `ProtectHome=tmpfs`).
*   **Persistent Sandboxing**: Stdio transport protocol is used to communicate with the running server.

---

## 3. Tool Registration & LLM Constraints

Discovered MCP tools are registered dynamically with Turbostar's tool registry.

### 3.1. Tool Names & Colons
*   **Naming Format**: Discovered tools will be exposed inside the editor configuration using the colon naming scheme:
    *   `mcp:<server_name>:<tool_name>` (e.g., `mcp:git-helper:show_diff`).
*   **LLM API Constraints**: Since LLM providers (Google Gemini, OpenAI, Claude) restrict function names to alphanumeric, underscores, and hyphens (`^[a-zA-Z0-9_-]+$`), Turbostar's LLM client transport layer will automatically serialize colons to double underscores (e.g., `mcp__server_name__tool_name`) before sending tool schemas to the model, and deserialize them back on invocation.

---

## 4. UI Integration (TUI Dialogs)

Two levels of TUI controls are implemented:

1.  **Main MCP Control Dialog**:
    *   A single, unified dialog showing both Global and Project MCP servers.
    *   Visually split into distinct sections/headers for "Global MCPs" and "Project MCPs".
    *   Enables toggling each server on/off.
2.  **Tool Configuration Dialog**:
    *   Pressing `Enter` on a selected server in the main dialog opens a child dialog listing all tools exposed by that specific MCP server.
    *   Allows granularly toggling individual tools on/off.

---

## 5. Persistence

Toggled states are persisted in the standard configuration locations using the existing INI format:
*   **System MCP Settings**: Saved in `~/.turbostar` under `mcp.<server_name>.enabled` and `mcp.<server_name>.<tool_name>.enabled` keys.
*   **Project MCP Settings**: Saved in the project's `config.ini` file.

---

## 6. Lifecycle Management

To ensure resource efficiency and clean states:
*   **Activation**: When an MCP server is enabled (or the editor starts up with enabled servers), its subprocess is spawned via the appropriate sandboxed runner profile.
*   **Deactivation**: If the user disables an MCP server through the configuration TUI, the running subprocess is terminated immediately (via SIGTERM and SIGKILL fallbacks if needed), and its exposed tools are dynamically removed from the active `tool_registry` to prevent any invalid calls.
*   **Exit Cleanup**: Upon editor shutdown, all active MCP server processes are cleanly terminated.

