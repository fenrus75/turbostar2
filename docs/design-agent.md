# LLM Agent Architecture (Turbostar)

This document describes the core architecture of the LLM Agent subsystem in Turbostar, which is located in `src/agentlib/`.

For information on how to implement specific tools for the agent, see `docs/design-tools.md`.

## Core Agent Loop
The central coordinator is the `ai_agent` class, which interacts with the `ai_model` and `llm_client`. 
- **`ai_agent`**: Manages the high-level conversation loop.
- **`llm_client`**: Handles the communication with the LLM backend (e.g., Gemini API), formatting requests and parsing raw responses.

## Interactions Subsystem (`src/agentlib/interactions/`)
The `interactions` subsystem acts as the ViewModel bridging the gap between raw LLM API JSON and the Turbo Pascal-style TUI screen.

- **`agent_interaction` (Base Class)**: Defines the common interface for all interaction elements in the chat log. It handles text wrapping, line width calculations, and maintains a rendering cache (`cached_width_` and `cached_lines_`). It translates state into a `std::vector<interaction_line>` containing text and `color_pair` metadata.
- **Subclasses**:
  - `user_message`: Represents user input.
  - `llm_response`: The textual output from the LLM.
  - `tool_interaction`: Visualizes a tool call.
  - `system_message`: System-level prompts and context injected into the context window.
  - `reasoning`: Handles display of the agent's internal thought process/reasoning steps, if supported by the model.

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