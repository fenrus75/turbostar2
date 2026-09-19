# AI Agent (`ai_agent`)

## Goals
The `ai_agent` class is the central autonomous execution engine for AI interactions in Turbostar. It orchestrates the full model-agent lifecycle:
1. Managing conversational context, system prompts, active skills, and tool definitions.
2. Running the asynchronous prompt execution loop on a dedicated worker thread (`agent_thread_`).
3. Interfacing with `llm_client` for streaming responses, thinking traces, and tool calls.
4. Validating and executing tool calls via `tool_registry` and `tool_validator`.
5. Managing hierarchical multi-level context compaction (`compaction_engine`), semantic episode boundaries, and archival paging.
6. Managing child subagents (`subagent_manager`) and delegating subtasks.

## Architecture and Constraints
- **Asynchronous Execution Thread**: An agent runs prompt turns in `agent_thread_`, communicating with the UI thread via `event_queue` and status events (`agent_status::thinking`, `tool_execution`, `waiting`, `idle`).
- **Interaction History**: Stored as a polymorphic list of `std::shared_ptr<interaction>` objects (user messages, system notices, tool requests, and assistant responses), rendered by `agent_window`.
- **Compaction & Paging Architecture**:
  - Context is partitioned into discrete `Episode` segments.
  - As tokens approach context limits, the agent evaluates tier transitions: Level 0 (Raw) -> Level 1 (Think-Free) -> Level 2 (Think-Free + Pseudo) -> Paged Out (archived to disk).
  - Paged-out episodes are replaced with lightweight index entries (`episode_index_entry`) containing reactive restoration hints.
- **Thread Safety**: State transitions, interaction appends, and cancellation flags are guarded by internal mutexes (`mutex_`) and condition variables (`cv_`).

## Lessons Learned
- **Prompt vs Interaction Separation**: Formatting interactions directly to model payloads without an intermediate translation layer caused formatting drift across providers. The conversation formatter must slice messages cleanly according to the model's API protocol.
- **Emergency Compaction on Context Overflow**: When LLM endpoints return token overflow errors, triggering emergency immediate compaction of earlier turns and retrying the request prevents terminal session aborts.
- **Cancelling Subagent Trees**: Cancelling a parent agent must recursively cancel all active child subagents to avoid orphan background threads consuming API tokens.
