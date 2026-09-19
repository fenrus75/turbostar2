# Tool Registry (`tool_registry`)

## Goals
The `tool_registry` class serves as the central registration, schema export, validation, and execution coordinator for all LLM tools in Turbostar. It implements the two-stage security pipeline and manages tool family access control.

## Architecture and Constraints
- **Self-Registering Tool Architecture**:
  - Tools register themselves via `tool_registrar` helper objects at static initialization time using factory functions (`validator_factory`).
  - Decoupled from core agent classes: new tools can be added by simply implementing a validator and tool class without editing a central switch.
- **Two-Stage Execution Pipeline**:
  - `prepare_tool`:
    - *Stage 1 (Syntactic)*: Validates argument JSON types and required fields against the registered schema. Normalizes snake_case / PascalCase parameter variations.
    - *Stage 2 (Contextual)*: Verifies dynamic runtime preconditions (e.g., file existence, path sandboxing via `file_security_manager`).
  - `execute_prepared_tool`: Executes the validated tool with centralized document auto-saving, execution statistics tracking, exception containment, and tool call tracing (`tool_tracer`).
- **Tool Families & Permissions**:
  - Maps tools to tool families (`base`, MCP server names, custom plugin families).
  - Filters active tools based on agent properties, mutation capabilities (`is_mutation_possible`), and user configuration.

## Lessons Learned
- **Preparing Tools Before Execution**: Splitting tool handling into `prepare_tool` and `execute_prepared_tool` allows asynchronous agents to validate parallel tool calls upfront, fail fast on invalid arguments, and cleanly interleave execution.
- **Auto-Saving Dirty Documents**: Automatically saving modified open buffers prior to running build, test, or git tools guarantees external child processes see the user's latest code changes without manual save dialogs.
