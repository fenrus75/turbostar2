# Turbostar Context Compaction Strategy

As agents tackle multi-day, complex software engineering tasks, their context windows (conversation histories) inevitably bloat. This document outlines the architectural blueprint for "Context Compaction"—managing token limits without inducing agent amnesia, while strictly optimizing for LLM API cost structures.

## 1. The Economics of Compaction (Prompt Caching)

Modern LLM APIs (Gemini, OpenAI, Anthropic) utilize **Prompt Caching** (or Context Caching) to reduce costs and latency. They do this by recognizing exact prefix matches.

*   **The Problem with Gradual Pruning:** If the engine continuously deletes or modifies the oldest messages (a "sliding window"), the prefix changes on every turn. This entirely destroys the API cache, resulting in 100% full-price billing for every token on every request.
*   **The Solution (Step-Function Resets):** The optimal strategy is to let the history grow statically (maximizing cache hits and reducing costs) until a semantic boundary is reached. At that boundary, a harsh "Step Function Reset" is performed. We pay the cache-miss penalty exactly *once* to establish a new, lean prefix, and then caching resumes.

## 2. The Semantic Lifecycle Engine

Humans do not forget information based on token limits; memory is segmented by tasks, sleep, and milestones. Turbostar's compaction engine models this via a **Semantic Lifecycle Engine** driven by "Anchor Points."

### Triggers for Anchor Points
An anchor point indicates that a major task or context shift has occurred. Triggers include:
1.  **Deterministic Milestones:** Execution of tools like `git_commit` (task saved), `fs_run_tests` returning 100% pass, or `pop_todo()` (moving to a new task).
2.  **Temporal Gaps:** If the time between the last interaction and a new user prompt exceeds a threshold (e.g., 4 hours), it strongly implies a new session/focus.
3.  **LLM Self-Reporting:** An explicit, zero-cost tool (e.g., `agent_mark_milestone`) that the agent calls when it believes it has resolved the core request or is pivoting to a new architectural feature.

### Action on Anchor (The Archival Pointer & Context Paging) [PARTIALLY IMPLEMENTED]
When an anchor is triggered, the preceding bulk of the conversation is collapsed to save tokens, but it is **not permanently destroyed**. 

1.  **Serialization [IMPLEMENTED]:** The collapsed history is serialized into an archival file (e.g., `.cache/turbostar/history/milestone_5.json`) within the project's local workspace.
2.  **Pointer Injection [IMPLEMENTED]:** The massive block of turns is then purged from the active array and replaced with a single, highly dense `system` message summarizing the milestone, appended with an Archival Pointer: 
    *`[SYSTEM MEMORY: Milestone Reached] Time: 8:00 AM. Action: git_commit. Summary: Fixed memory leak in lsp_manager. Raw history archive: milestone_5`*
3.  **Context Restoration (Virtual Paging) [IMPLEMENTED (Backend)]:** By providing a dedicated tool like `agent_restore_milestone_context(milestone_id)`, the agent gains the ability to perform a true "task switch." If it needs to resume work on a deeply historical task, it can call this tool. Assuming the token budget allows, the C++ backend will deserialize the archive and natively re-inject the historical conversational turns back into the agent's active memory array, operating exactly like virtual memory paging for an OS.
    *   *Non-Destructive Optimized Page-In (Optional):* When paging in historical context, options can be provided (e.g., `strip_reasoning=true`, `truncate_outputs=true`) to load the historical tool calls and outputs while explicitly filtering out the original `reasoning_content` blocks or squashing old terminal outputs. Because the underlying archive on disk remains completely untouched, this optimization is non-destructive—the agent can always issue a second command to re-page the raw, uncompressed archive if it realizes it needs deeper details.
    *   *Progressive Stripping (Think-Free Tiers):* Because different LLM providers handle "thinking" differently, the optimized page-in utilizes a two-tiered heuristic. **Level 1** strips explicit `reasoning_content` fields (used by models like DeepSeek). **Level 2** strips conversational `content` *only* if the turn also contains a `tool_call`. This perfectly eliminates pseudo-reasoning bloat from models like GPT-4o (e.g., "I will now check the file..."), while safely preserving actual conversational text from turns where the agent was directly talking to the user.
    *   *Temporal Decay Paging (Smart Defaults):* The system will intelligently apply default compression levels based on the age of the archive being paged in. An archive from 10 minutes ago will be restored nearly intact, ensuring immediate context is vivid. An archive from 5 days ago will default to aggressive "think-free" progressive stripping and terminal truncation, mimicking human memory (where old events naturally fade into factual summaries).
4.  **Cross-Session Persistence [IMPLEMENTED]:** Because history is serialized, when the user closes and restarts the editor, the agent begins with a fully "paged-out" context. It is provided a lightweight index of past milestones, allowing it to instantly task-switch back into yesterday's context by paging it in, providing true multi-day persistence without the token overhead.
5.  **Hierarchical Milestone Indexing & Tagging [PLANNED]:** Over months of development, a flat list of milestones will itself become too large. Archival pointers will be structured hierarchically (e.g., `Epic -> Task -> Milestone`) and support **LLM-assigned tagging**. When creating a milestone, the LLM can assign arbitrary tags (e.g., `[ui-refactor, memory-leak]`). This allows the agent to use tools like `agent_restore_milestone_context(tag="ui-refactor")` to gracefully page-in a bulk set of related historical contexts without having to track specific numerical milestone IDs.

## 3. Proactive (Cache-Preserving) Compaction

The absolute best way to manage context without breaking the API prompt cache is to prevent bloat from entering the history in the first place.

*   **Instant Tool Summarization [IMPLEMENTED]:** If a high-volume tool (like a build script or test runner) executes successfully, we intercept the payload *before* it is appended to the conversation history. For example, a successful `fs_compile_project` that generates 5,000 lines of OK messages is instantly parsed and reduced to: `"Build succeeded. [0 Errors]. Warnings: <list of warnings>"`. By shrinking the payload proactively, the prompt cache builds upon the summarized version, entirely avoiding the cache invalidation penalty of retroactive pruning.
*   **Ephemeral Error Zapping (Inner Correction Loop) [IMPLEMENTED]:** When an agent triggers a transient error (e.g., a Stage 1 Security Violation for bad arguments, or a syntax error), the failure and the agent's retry attempts are held in a temporary buffer. Once the agent successfully corrects the call, the entire failure loop is "zapped" (discarded). Only the final, successful tool call is committed to the permanent conversation history. 
    *   *Constraint 1 (Same Tool Only):* Zapping is only permitted if the retry utilizes the exact same tool. If the agent recovers by using a different tool (e.g., failing a file read, then listing the directory to find the correct name), the failure chain must be preserved so the agent's contextual discovery path makes sense.
    *   *Constraint 2 (The Multi-Strike Rule):* The zapping applies to "fail -> think -> pass" and extended "fail -> think -> fail -> pass" apology loops. However, if the loop is aborted (the agent gives up), the history must be preserved for external debugging.
    *   *Risk Consideration:* Zapping the intermediate "think" blocks destroys any generalized meta-knowledge the agent deduced during the failure (e.g., "Ah, I must always use absolute paths in this project").
*   **Tool Call Merging (Context Coalescing) [PLANNED]:** If an agent performs sequential, adjacent operations—such as requesting lines 1-100 of a file, and immediately following up with a request for lines 101-149—these distinct turns can be intercepted and coalesced into a single historical record (a single tool call requesting lines 1-149 and returning the combined output). This eliminates the redundant JSON schema overhead and assistant thought headers between the disjointed calls.
*   **ANSI Clear Screen Semantics [IMPLEMENTED]:** If an agent executes a terminal command and the resulting payload contains an ANSI "Clear Screen" escape sequence (e.g., `\033[2J` or `\033[H`), we treat this as a semantic reset signal from the program. We can proactively truncate all terminal output that occurred *prior* to that escape sequence before committing the result to the conversation history, naturally eliminating stale UI renders or splash screens.

## 4. Retroactive Pruning Strategies

Before a full Anchor reset is required, smaller, highly surgical pruning techniques can be applied to older history, recognizing that these *will* break the cache prefix and incur a one-time processing cost.

*   **"Think" Pruning:** Advanced reasoning models (like Gemini 2.0 Thinking or OpenAI o1/o3) generate massive internal "reasoning" or "thought" blocks before outputting their final response. Once a turn is in the past, its reasoning block is likely obsolete because the *conclusion* of that reasoning is fully captured in the agent's final text and tool calls. We can strip `reasoning_content` from all older turns, reclaiming thousands of tokens per turn.
*   **Semantic Deduplication:** If an agent calls `fs_read_lines` on `main.cpp`, and then 5 turns later calls it again, the *first* output is entirely obsolete. The older result can be replaced with `[Output pruned: File read again in later turn]`.
*   **Terminal Output Screenshotting (Head/Tail):** Massive outputs (compilers, test suites) rarely need 10,000 lines of context. Using Turbostar's native `gcc_log_parser`, we can identify errors and aggressively truncate the output. We retain the first 20 lines (Head), the specific error blocks, and the last 20 lines (Tail). 
*   **Temporal Decay for Terminals:** The older a terminal output gets, the more aggressively it is truncated. A compile from 1 turn ago might keep 500 lines; a compile from 10 turns ago shrinks to just 25 lines.

## 5. Advanced Future Heuristics / TODOs

*   **Automated Decision Engine (Memory Eviction):** Currently, the `agent_mark_milestone` tool explicitly separates *snapshotting* (saving to disk) from *paging out* (deleting from active RAM). Snapshots should always be taken immediately upon task completion for persistence. However, paging out should ideally be managed by a background Decision Engine. This engine would dynamically monitor the agent's active token usage against its `max_context_tokens` limit. When the budget gets tight, the engine evaluates existing snapshotted milestones and autonomously evicts (pages out) the oldest or least relevant ones, providing the LLM with seamless, automated garbage collection.
*   **Local DNN Context Scorer:** A lightweight, local neural network (e.g., a simple Multi-Layer Perceptron running locally in C++) that evaluates the semantic relevance of each turn. Instead of hardcoded rules, the DNN recommends which specific tool outputs or reasoning blocks are safe to collapse without damaging the agent's current logical trajectory.
