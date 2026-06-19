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

# Open Design Decisions

- **Q1: Protocol vs. Transport Decoupling**
  - *Option A (Decoupled)*: The `Connection` is a protocol-specific layer (OpenAI payload format, Gemini payload format) that delegates the actual HTTP networking and test recording/replay mocks to an `llm_transport` object (e.g., `httplib_transport`, `replay_transport`).
  - *Option B (Unified)*: The `Connection` incorporates transport directly (each connection vendor has its own transport logic, and recording/replaying is handled by specialized mock connection classes).
  - *Status*: Postponed until other elements of the protocol redesign are more concrete.