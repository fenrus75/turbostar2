# `inspector_coordinator`

## Goals
- Serve as the central registry and query dispatcher for all Symbol Inspector providers.
- Conduct fast, synchronous evaluations across all candidate providers and select the highest-scoring content.
- Manage asynchronous lifecycle promotions cleanly, automatically dispatching `query_async` when providers return `is_final == false`.
- Provide monotonic request ID sequencing to prevent stale asynchronous results from clobbering new navigation positions.

## Constraints
- Safe for concurrent provider registration and query dispatch.
- Zero allocations on query evaluation fast paths.
- Synchronous phase must execute in < 1ms.

## Lessons Learned
- Copying raw provider pointers into a local stack vector under a short lock allows query execution and async callbacks to proceed without holding the registry mutex during provider execution, eliminating deadlock potential.
