# Replay Transport (`replay_transport`)

## Goals
The `replay_transport` class is a mock `llm_transport` implementation that loads pre-recorded conversation tapes and emulates LLM endpoint responses without requiring any live network access or external API credentials.

## Architecture and Constraints
- **Offline Determinism**: Powers `agentcli_replay` and unit test suites, enabling 100% deterministic verification of agent reasoning loops, tool call invocations, error retries, and compaction passes.
- **Tape Matching**: Matches outgoing requests against recorded tape entries by hashing request content or sequence order, returning the exact recorded response payload or SSE chunk stream.
- **Simulated Delays**: Can optionally sleep between streamed chunks according to the recorded delays to verify UI streaming animations and cancellation hooks.

## Lessons Learned
- **Request Invariant Drift**: Minor prompt formatting changes (such as whitespace tweaks or tool schema parameter reordering) can break tape matching if matching is based on strict full-string hashes. Normalizing JSON payloads before matching improves test resilience across tool schema refactors.
- **Zero Network In CI**: Utilizing `replay_transport` in CI environments guarantees test suites run in milliseconds, never fail due to upstream API outages, and incur zero ongoing token expenses.
