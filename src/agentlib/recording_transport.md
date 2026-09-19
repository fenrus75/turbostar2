# Recording Transport (`recording_transport`)

## Goals
The `recording_transport` class is an intercepting decorator that wraps an underlying live network transport (e.g. `httplib_transport`) to record raw HTTP requests, headers, and streamed SSE responses directly to disk fixtures for subsequent offline test replay.

## Architecture and Constraints
- **Decorator Pattern**: Implements `llm_transport`, intercepting `post()` and `post_stream()` calls, forwarding them to the real underlying transport, and buffering incoming data chunks in memory.
- **Fixture Generation**: Serializes completed interactions (request method, path, headers, request body, status code, response headers, streamed chunks, and timing delays) into structured JSON fixture files (`tape.json`).
- **Headless Tool Integration**: Powers the `agentcli_record` utility to generate repeatable integration test suites against live models.

## Lessons Learned
- **Chunk Timing Fidelity**: Replaying streamed responses at instantaneous speed can mask subtle race conditions in UI event queues. Preserving realistic inter-chunk timing metadata allows replay tools to emulate live streaming accurately.
- **Header Sanitization**: Strip or redact sensitive bearer tokens and API keys during recording to prevent accidental credential leakage into test repository fixtures.
