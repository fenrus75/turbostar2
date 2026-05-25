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

## 3. Incremental Pruning Strategies

Before a full Anchor reset is required, smaller, highly surgical pruning techniques can be applied to massive tool outputs, provided they do not heavily disrupt the cached prefix or if they are applied immediately upon tool execution.

*   **Semantic Deduplication:** If an agent calls `fs_read_lines` on `main.cpp`, and then 5 turns later calls it again, the *first* output is entirely obsolete. The older result can be replaced with `[Output pruned: File read again in later turn]`.
*   **Terminal Output Screenshotting (Head/Tail):** Massive outputs (compilers, test suites) rarely need 10,000 lines of context. Using Turbostar's native `gcc_log_parser`, we can identify errors and aggressively truncate the output. We retain the first 20 lines (Head), the specific error blocks, and the last 20 lines (Tail). 
*   **Temporal Decay for Terminals:** The older a terminal output gets, the more aggressively it is truncated. A compile from 1 turn ago might keep 500 lines; a compile from 10 turns ago shrinks to just 25 lines.

## 4. Advanced Future Heuristics

*   **Local DNN Context Scorer:** A lightweight, local neural network (e.g., a simple Multi-Layer Perceptron running locally in C++) that evaluates the semantic relevance of each turn. Instead of hardcoded rules, the DNN recommends which specific tool outputs or reasoning blocks are safe to collapse without damaging the agent's current logical trajectory.
