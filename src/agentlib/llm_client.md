# LLM Client (`llm_client`)

## Goals
The `llm_client` class bridges high-level agent conversation structures and tool schemas with low-level network transports. It transforms internal conversation histories (`message` vectors) into provider-specific API payloads (OpenAI, Gemini, Anthropic Claude, Copilot, Response format) and translates responses back into typed agent deltas.

## Architecture and Constraints
- **Multi-Protocol Formatter**: Adapts message hierarchies, tool definitions, and system instructions into the exact wire format expected by the model's protocol type:
  - Formats tool schemas into OpenAI `tools` arrays or Gemini function declarations.
  - Slices messages appropriately for stateful `openai_response` chains using `previous_response_id`.
- **Streaming & Sync Interfaces**:
  - `send_chat`: Synchronous execution returning full `llm_chat_response`.
  - `send_chat_stream`: Dispatches real-time text chunks, thinking fragments, and tool arguments to a caller-supplied callback.
- **Server-Side Compaction**:
  - `compact_response`: Dispatches compaction requests to `/v1/responses/compact` for non-mutating stateful sessions.
- **Transport Decoupling**: Accepts an abstract `llm_transport` interface, allowing hot-swapping between real network clients (`httplib_transport`), recorder transports (`recording_transport`), and offline test mocks (`replay_transport`).

## Lessons Learned
- **Transport Abstraction for Testing**: Decoupling the client from concrete network sockets enables full agent conversation workflows to be recorded and replayed deterministically in headless CI test suites without live API keys.
- **Thinking Tag Separation**: Newer reasoning models (DeepSeek-R1, Gemini 2.0 Flash Thinking, Claude 3.7 Sonnet) emit thinking blocks either as separate delta fields or wrapped in `<think>...</think>` tags. Normalizing these in `llm_client` ensures `agent_window` can display reasoning traces in dedicated collapsible UI blocks.
