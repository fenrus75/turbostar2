# Subagent Manager (`subagent_manager`)

## Goals
The `subagent_manager` class manages the definition, discovery, indexing, and synthesis of specialized subagent personas in Turbostar. Subagents are tailored agents with distinct system prompts, restricted tool families, customized animations, and domain-specific roles (e.g., code reviewer, card synthesizer, security scanner).

## Architecture and Constraints
- **Subagent Discovery & Scanning**:
  - Scans system and project directories (`agents/`, `.turbostar/agents/`) for markdown (`.md`) and JSON agent definition files containing YAML frontmatter and system prompt text.
  - Exposes dynamic registration interfaces (`register_subagent`, `unregister_subagent`) for C++ plugins.
- **A2A (Agent-to-Agent) Protocol Integration**:
  - Synthesizes industry-standard Agent-to-Agent (A2A) Agent Card JSON files (`generate_a2a_card_for_agent`) describing input/output JSON schemas, protocols, and skill capabilities.
  - Implements 3-tier card resolution (`get_a2a_card`): checking on-disk `.card.json`, memory-cached cards, and programmatic fallback synthesis.
- **Concurrency**:
  - Protected by `std::shared_mutex subagents_mutex_`. Read queries take shared locks; mutations take exclusive locks.

## Lessons Learned
- **Shared Mutex for Heavy Queries**: Agent creation and prompt routing frequently look up subagent definitions concurrently across multiple worker threads. Using `std::shared_mutex` eliminates reader contention while allowing clean plugin unloading.
- **Strict Role-to-Tool Scoping**: Enforcing tool family permissions directly at the subagent level prevents specialized read-only subagents (like code reviewers) from executing destructive filesystem mutations.
