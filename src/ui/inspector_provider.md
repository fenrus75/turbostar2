# `inspector_provider` and `inspector_coordinator`

## Goals
- Provide a modular, pluggable architecture for supplying contextual information to the Symbol Inspector companion window.
- Decouple content providers from ncurses and the UI rendering pipeline completely. Providers output pure `std::string` with a Markdown Lite syntax and a quality score (0–100).
- Support dynamic scoring where multiple providers compete for window ownership; the window always presents content from the highest-scoring provider.
- Accommodate graduated asynchronous resolution (e.g. LSP or remote LLM queries) where an initial low/provisional score is seamlessly superseded by higher-ranked rich content when background operations complete.
- Ensure thread safety: providers may execute in worker threads, and coordinators safely dispatch and filter callbacks against monotonic sequence IDs.

## Constraints
- Providers must not include or invoke ncurses drawing routines.
- Synchronous `query()` methods must be non-blocking and execute in under 1ms.
- Any network, LSP, or disk-heavy resolution must be executed in `query_async()` on detached threads.
- Providers must format output using the Markdown Lite syntax: `**bold**` (headers), `` `code` `` (symbols/identifiers), `*italic*` (status/hints), `1.` / `2.` (numbered parameters), and `[label](target)` (locations).

## Lessons Learned
- Decoupling content providers from UI rendering prevents concurrency bugs: ncurses is not thread-safe, so allowing providers to draw directly caused race conditions. Emitting structured `inspector_result` objects containing a `score` and a `std::string` markdown payload completely solves cross-thread synchronization.
- Monotonic request IDs are crucial when cursor navigation is rapid: without them, an asynchronous response for a previous cursor coordinate could arrive late and clobber newer inspection data.
