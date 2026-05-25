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

### Action on Anchor
When an anchor is triggered, the preceding bulk of the conversation is collapsed. Dozens of turns of intense debugging (and massive tool outputs) are purged from the active array and replaced with a single, highly dense `system` message summarizing the milestone (e.g., "Time: 8:00 AM. Action: git_commit. Summary: Fixed memory leak in lsp_manager").

## 3. Proactive (Cache-Preserving) Compaction

The absolute best way to manage context without breaking the API prompt cache is to prevent bloat from entering the history in the first place.

*   **Instant Tool Summarization:** If a high-volume tool (like a build script or test runner) executes successfully, we intercept the payload *before* it is appended to the conversation history. For example, a successful `fs_compile_project` that generates 5,000 lines of OK messages is instantly parsed and reduced to: `"Build succeeded. [0 Errors]. Warnings: <list of warnings>"`. By shrinking the payload proactively, the prompt cache builds upon the summarized version, entirely avoiding the cache invalidation penalty of retroactive pruning.
*   **Ephemeral Error Zapping (Inner Correction Loop):** When an agent triggers a transient error (e.g., a Stage 1 Security Violation for bad arguments, or a syntax error), the failure and the agent's retry attempts are held in a temporary buffer. Once the agent successfully corrects the call, the entire failure loop is "zapped" (discarded). Only the final, successful tool call is committed to the permanent conversation history. 
    *   *Constraint 1 (Same Tool Only):* Zapping is only permitted if the retry utilizes the exact same tool. If the agent recovers by using a different tool (e.g., failing a file read, then listing the directory to find the correct name), the failure chain must be preserved so the agent's contextual discovery path makes sense.
    *   *Constraint 2 (The Multi-Strike Rule):* The zapping applies to "fail -> think -> pass" and extended "fail -> think -> fail -> pass" apology loops. However, if the loop is aborted (the agent gives up), the history must be preserved for external debugging.
    *   *Risk Consideration:* Zapping the intermediate "think" blocks destroys any generalized meta-knowledge the agent deduced during the failure (e.g., "Ah, I must always use absolute paths in this project").
*   **Tool Call Merging (Context Coalescing):** If an agent performs sequential, adjacent operations—such as requesting lines 1-100 of a file, and immediately following up with a request for lines 101-149—these distinct turns can be intercepted and coalesced into a single historical record (a single tool call requesting lines 1-149 and returning the combined output). This eliminates the redundant JSON schema overhead and assistant thought headers between the disjointed calls.

## 4. Retroactive Pruning Strategies

Before a full Anchor reset is required, smaller, highly surgical pruning techniques can be applied to older history, recognizing that these *will* break the cache prefix and incur a one-time processing cost.

*   **"Think" Pruning:** Advanced reasoning models (like Gemini 2.0 Thinking or OpenAI o1/o3) generate massive internal "reasoning" or "thought" blocks before outputting their final response. Once a turn is in the past, its reasoning block is likely obsolete because the *conclusion* of that reasoning is fully captured in the agent's final text and tool calls. We can strip `reasoning_content` from all older turns, reclaiming thousands of tokens per turn.
*   **Semantic Deduplication:** If an agent calls `fs_read_lines` on `main.cpp`, and then 5 turns later calls it again, the *first* output is entirely obsolete. The older result can be replaced with `[Output pruned: File read again in later turn]`.
*   **Terminal Output Screenshotting (Head/Tail):** Massive outputs (compilers, test suites) rarely need 10,000 lines of context. Using Turbostar's native `gcc_log_parser`, we can identify errors and aggressively truncate the output. We retain the first 20 lines (Head), the specific error blocks, and the last 20 lines (Tail). 
*   **Temporal Decay for Terminals:** The older a terminal output gets, the more aggressively it is truncated. A compile from 1 turn ago might keep 500 lines; a compile from 10 turns ago shrinks to just 25 lines.

## 5. Advanced Future Heuristics

*   **Local DNN Context Scorer:** A lightweight, local neural network (e.g., a simple Multi-Layer Perceptron running locally in C++) that evaluates the semantic relevance of each turn. Instead of hardcoded rules, the DNN recommends which specific tool outputs or reasoning blocks are safe to collapse without damaging the agent's current logical trajectory.
