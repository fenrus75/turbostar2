# Automatic Software Map: Design Brainstorm

## Goal
Provide the LLM agent with a high-level architectural view of the codebase by automatically generating a Markdown table of key classes, functions, their hierarchy, and their locations. This will be appended to system prompts.

## Core Strategy: Pure LSP & Caching
1. **No Regex:** We will rely entirely on the Language Server Protocol (LSP) using requests like `workspace/symbol` to seed our map with accurate locations.
2. **Caching:** Due to the cost of LSP queries, the map will be cached persistently in `~/.cache/turbostar/<project_hash>/software_map.json`.
3. **Instant Availability:** On startup, the cached (even if slightly stale) version is loaded immediately to serve the agent without blocking. A background thread will incrementally refresh and improve the map.

## The Scaling Problem & Statistical Sampling
In a codebase with 10,000+ functions, we cannot query the exact reference count for every single one. We need to identify the "important" functions (those called frequently by many different parts of the code).

### The Proposed Algorithm
1. **The Data Structure:** Maintain a known list of symbols. Each symbol has two counters:
   - `looked_up_count`: The exact number of inbound references (who calls this?), verified via an expensive LSP query (`textDocument/references` or `callHierarchy/incomingCalls`).
   - `accumulated_count`: A statistical estimate of importance.
2. **The Sampling Loop (Background Thread):**
   - Pick a function to sample. We can weight this randomly or prioritize functions with a high `accumulated_count` that haven't been exact-verified recently.
   - **Step A:** Query the exact inbound callers for this function and store it in `looked_up_count`.
   - **Step B:** Query the *outbound* calls for this function (what does *it* call?). For every target function it calls, increment that target's `accumulated_count` by 1.
3. **The Result:** "Hot" utility functions or core APIs will rapidly accumulate hits in `accumulated_count` as we randomly sample the broader codebase. Once their estimated count gets high enough, the loop naturally selects them for an exact `looked_up_count` verification.

## Potential Holes & Design Considerations

1. **LSP Outbound Calls (The "What does it call?" problem):**
   - *The Challenge:* Finding *inbound* references is easy (`textDocument/references`). Finding *outbound* calls historically required parsing the function body.
   - *The Solution:* We must rely on the LSP `Call Hierarchy` feature. We send `textDocument/prepareCallHierarchy` on the sampled function, followed by `callHierarchy/outgoingCalls`. We need to verify our `clangd` setup supports this reliably.

2. **The "Cold Start" Problem:**
   - On the very first launch of a massive project, the cache is empty. If we rely purely on uniform random sampling, it might take a long time to stumble upon the true core architecture.
   - *Mitigation:* When seeding the initial list via `workspace/symbol`, we could apply a small initial weight based on file path. Symbols defined in `include/` or top-level `src/` headers get a slight `accumulated_count` boost over static functions deep in `src/subsystem/impl/`.

3. **Cache Invalidation (Staleness & Churn):**
   - If a massive refactor occurs, a previously "hot" function might become orphaned. If we never sample it again, its cached `looked_up_count` remains falsely high.
   - *Mitigation (Git HEAD Tracking):* For projects managed by Git, we will store the Git hash of `HEAD` alongside the cached map. On startup, we check how many commits have occurred since that hash. This commit distance acts as a proxy for "codebase churn."
   - *Dynamic Aggressiveness:* If there are zero new commits, we can pause or significantly slow down the background scanner. If there are many commits, we increase the scanning aggressiveness to quickly catch up. Additionally, local unsaved file edits tracked by `project_manager` can trigger targeted mini-invalidations for symbols in those specific files.

4. **Classes vs. Functions:**
   - The sampling algorithm is perfect for execution flow (functions). For classes, importance is often structural (Inheritance/Composition). 
   - *Mitigation:* For classes, `typeHierarchy/subtypes` (how many things inherit from me?) and `textDocument/references` (how many things instantiate me?) can be run on a similar sampled basis, perhaps prioritized by classes with the most methods.

## Output Format
The agent needs a concise table. The background thread will periodically serialize the top *N* (e.g., Top 50) verified classes and functions into a markdown block.

| Symbol Name | Type | Importance | File Location | Key Relationships |
| :--- | :--- | :--- | :--- | :--- |
| `project_manager` | Class | High (120 refs) | `src/project_manager.h` | Base: `none`, Derived: `none` |
| `trim_trailing_whitespace` | Function | High (85 refs) | `src/markdown_utils.h` | |