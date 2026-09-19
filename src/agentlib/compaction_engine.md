# Compaction Engine (`compaction_engine`)

## Goals
The `compaction_engine` class plans and calculates progressive context compression transitions for conversational histories in `ai_agent`. It ensures that active conversations stay safely below model context window limits without discarding essential context prematurely.

## Architecture and Constraints
- **Tiered Compaction Levels**:
  - `Level 0` (Raw): Complete original user prompts, thinking blocks, and detailed tool outputs.
  - `Level 1` (Think-Free): Model thinking/reasoning blocks removed; tool calls and textual content preserved.
  - `Level 2` (Think-Free + Pseudo): Strips both native thinking blocks and simulated XML thought tags.
  - `Paged Out`: Entire episode moved to disk archive, replaced by an `episode_index_entry` stub in the prompt.
- **Planning Algorithm (`plan_compaction`)**:
  - Evaluates active episodes sorted by Least Recently Used (LRU) access order (`lru_seq`).
  - Progressively escalates compression tiers for oldest episodes first until total estimated tokens drop below `target_tokens`.
  - Emits an ordered list of `transition` actions for the agent to execute.

## Lessons Learned
- **Progressive Degradation Over Immediate Eviction**: Rather than immediately paging out entire episodes, stripping reasoning traces first preserves high semantic context density with zero information loss regarding factual tool actions and code changes.
- **LRU Sequence Tracking**: Relying solely on chronological turn order fails when an agent revisits or reactivates an older episode. Tracking `lru_seq` ensures recently referenced historical episodes are preserved over inactive intermediate episodes.
