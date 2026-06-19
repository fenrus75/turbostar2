# LLM  Connection refactor/redesign

Goal: Get a modular setup where different connection types are neatly separated

# Overall concept

We make a "connection" (mostly abstract) class for which specific connection types 
inherit. Connection types are based on their protocol (inclusive of transport), e.g. the "OpenAI Completion", "Gemini", "Copilot", "OpenAI Response"
protocols, and possibly more to come over time.
We also have the local recording and replay "protocols"


# files and locatioms

- location: `src/agentlib/protocols/`
- one .cpp per class, one .h per class

# Major flow

A Connection fundamentally belongs to the agent class and is created based on the model that is in use,
and persists over time, as long as that model is active for the agent (to allow the http connection to stay open etc).
- **Lifecycle (Q2 Alignment)**: On model change, the agent explicitly closes the current connection and instantiates a new one for the new model.
- **Sync History (Q3 Alignment)**: Before any prompt submission or turn processing, the agent calls `sync_history(convo)` on the connection. The connection checks if the conversation's history has been invalidated, performs any necessary session reset on the server side, and clears the invalidation flag.

All major methods of the Connection will need to have a "Conversation" class (pointer) as argument, as they 
operate on the Conversation -- either by retrieving things to send to the server from it,
or by (indirectly?) placing responses in it.
 
Known key operations
- authenticate/establish http(s) connection -- should get a Model class passed in for this to get the server/auth info
- establish a new logical connection based on the history from the Conversation -- depending on the protocol, this may need sending a bunch of context to the server 
- sending user and system turns
- receiving responses 
- deal with is_history_invalidated() correctly

- **Stream Event Routing**: The Connection exposes streaming methods to the agent using a callback pattern. It emits structured stream events (e.g., `content_chunk`, `reasoning_chunk`, `tool_call_delta`, `completed`, `error`) containing incremental response data. The `ai_agent` acts as the router, digesting these events, updating the `Conversation` data model, and coordinating tool execution on completion.
- **Connection Metadata Serialization**:
  - The `Conversation` stores a serializable `connection_state` JSON block.
  - To prevent stale states across restarts or model switches, this JSON stores a signature (`model_id` and `protocol`) alongside the `metadata`.
  - Connections validate this signature on load. If it doesn't match the current session, or if the server rejects a restored `session_id` (e.g., due to expiration), the connection invalidates the state and falls back to rebuilding the session context from the conversation history.

# Tool Declaration & Decoupling

To prevent vendor-specific tool formatting logic from leaking into the `tool_registry` class (which currently has hardcoded methods like `get_tools_json()` and `get_gemini_tools_json()`):
- **Registry Abstraction**: The `tool_registry` exposes a generic getter returning a list of active, allowed `tool_validator` instances:
  ```cpp
  std::vector<std::shared_ptr<tool_validator>> get_active_tools(
      const std::vector<std::string> &active_families, 
      bool mutation_possible, 
      const std::string &agent_identity // Or agent_role enum
  ) const;
  ```
- **Agent Identity Mapping**: The `Connection` must know the active agent's identity (currently represented as the `agent_role` enum, but soon transitioning to a `std::string`) to query the registry correctly. This identity will be passed from the `ai_agent` as context options during prompt/streaming calls.
- **Provider-Specific Formatting**: Each `Connection` subclass iterates over this validator list to construct the exact JSON payload expected by the provider (e.g., nesting functions in OpenAI/Copilot format, double-nesting function declarations for Gemini, or mapping schema parameter names like `input_schema` for Anthropic).

# Statistics & Token Usage

To resolve bugs where token counts and billing costs are double-counted or misreported:
- **Internal Accumulation**: The `Connection` subclass is responsible for parsing provider-specific usage payloads (e.g. standard `usage` keys for OpenAI, or `usageMetadata` with `promptTokenCount`/`candidatesTokenCount` keys for Gemini).
- **Exactly-Once Emission**: During streaming, the connection stores the parsed usage metadata internally. It does **not** emit usage fields in intermediate stream chunk events.
- **Completion Event**: Once the stream completes, the connection emits a final `completed` event containing the consolidated, final `llm_usage` statistics.
- **Agent Integration**: The `ai_agent` intercepts this single `completed` event to update session/turn counters and record model usage costs exactly once, avoiding cumulative `+=` bugs on streaming chunks.

# Open Design Decisions

- **Q1: Protocol vs. Transport Decoupling**
  - *Option A (Decoupled)*: The `Connection` is a protocol-specific layer (OpenAI payload format, Gemini payload format) that delegates the actual HTTP networking and test recording/replay mocks to an `llm_transport` object (e.g., `httplib_transport`, `replay_transport`).
  - *Option B (Unified)*: The `Connection` incorporates transport directly (each connection vendor has its own transport logic, and recording/replaying is handled by specialized mock connection classes).
  - *Status*: Postponed until other elements of the protocol redesign are more concrete.