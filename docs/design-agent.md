# LLM Agent Architecture (Turbostar)

This document describes the core architecture of the LLM Agent subsystem in Turbostar, which is located in `src/agentlib/`.

For information on how to implement specific tools for the agent, see `docs/design-tools.md`.

## Core Agent Loop
The central coordinator is the `ai_agent` class, which interacts with the `ai_model` and `llm_client`. 
- **`ai_agent`**: Manages the high-level conversation loop.
- **`llm_client`**: Handles the communication with the LLM backend (e.g., Gemini API), formatting requests and parsing raw responses.

## Interactions Subsystem (`src/agentlib/interactions/`)

The `interactions` subsystem acts as the ViewModel bridging the gap between raw LLM API JSON and the Turbo Pascal-style TUI screen.

### Core Architecture
- **`interaction_type`**: A high-level category (e.g., `user_message`, `reasoning`, `terminal`) used by the UI to group related interactions into a single visual "Turn."
- **`interaction_role`**: Defines the semantic purpose of a message (e.g., `agent`, `user`, `thinking`, `system`). This is used by the theme system to resolve colors independently of the interaction's implementation.
- **`agent_interaction` (Base Class)**: Defines the common interface. It handles text wrapping and maintains a rendering cache. It now requires implementations of `get_type()` and `get_role()`.
- **Subclasses**:
  - `user_message`: Represents user input.
  - `llm_response`: The textual output from the LLM.
  - `reasoning`: The agent's internal thought process.
  - `tool_interaction`: Visualizes a tool call and its result.
  - `terminal`: Displays raw output from external processes (e.g., Python execution).
  - `action`: A specialized class for high-level operations (like `fs_read_lines`).
  - `system_message`: Context and instructions.

## Themed Grouped Rendering

To maintain visual clarity and a cohesive TUI aesthetic, the `agent_window` does not simply render a flat list of interactions. Instead, it performs a **Grouping Pass**:

### Turn Containers
Multiple related interactions (e.g., a User prompt followed by a Thinking block and a Tool call) are merged into a single **Turn Box**. 
- Each box is enclosed in a solid UTF-8 frame (`┌─┐`).
- This eliminates "ragged" background edges and provides clear boundaries between conversation turns.
- **Alternating Backgrounds**: Turn boxes alternate between a **Primary (White)** and **Alternate (Cyan)** background, creating a "ledger" look that aids scannability.

### Sub-Panels
Within a Turn Box, individual interactions are separated by horizontal lines. For complex items like Reasoning or Terminal output, the separator includes a text label (e.g., `─── Thinking ───`) to create a "sub-panel" effect.

### Color Resolution (`agent_theme.h`)
Colors are resolved dynamically using `get_color_pair(role, background_mode)`. This ensures that a "User" message always uses the correct blue-ish foreground, whether it's on a white or cyan background.
- **Exceptions**: Certain high-density outputs keep their distinct backgrounds:
  - **Terminal**: Always "White on Black."
  - **Diffs**: Always "Syntax-themed Blue."

### Custom Tool Interactions
By default, the `ai_agent` provides a simple text-based interaction for tool calls and results. However, tools can take over their own UI representation to provide rich, dynamic feedback (like progress bars or formatted diffs) during execution:

1. **`llm_tool::get_interaction()`**: The `tool_registry::prepare_tool` pipeline instantiates the tool before execution. The `ai_agent` checks if the tool returns a custom `agent_interaction`. If provided, this custom interaction replaces the default UI logging.
2. **Dynamic UI Updates**: The `ai_agent` injects a `trigger_ui_update` callback into the `tool_context`. During a long-running `llm_tool::execute()` method, the tool can mutate its custom interaction object and invoke `ctx.trigger_ui_update()` to force the TUI to redraw the screen immediately, all without blocking the main event thread.

## Transport and Testing Layer
The agent uses a pluggable transport layer (implementing `llm_transport`) to facilitate network communication and enable deterministic End-to-End testing.

- **`httplib_transport`**: The live HTTP client used in normal operation.
- **`recording_transport`**: Captures live HTTP traffic to disk during test creation.
- **`replay_transport`**: Mocks the LLM by replaying recorded traffic. This allows E2E tests to run quickly and deterministically without hitting the live API or requiring API keys.

## Security
The agent enforces strict runtime security. All tools must undergo validation before execution. For full details on the two-stage security model, refer to `docs/design-tools.md`.