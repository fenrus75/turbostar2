# HTTPLib Network Transport (`httplib_transport`)

## Goals
The `httplib_transport` class provides a robust HTTP/HTTPS network transport implementation for LLM client communication, built on `cpp-httplib`. It supports both synchronous request/response cycles and Server-Sent Events (SSE) streaming for real-time model text generation and tool invocation.

## Architecture and Constraints
- **Streaming Response Processing**: Implements chunked response parsers to process SSE streams (`data: ...`), extracting text deltas, thinking blocks, and tool call argument fragments on the fly.
- **Connection Management & Retries**: Manages TCP connection reuse, keepalive headers, TLS verification, and configurable request timeouts.
- **Cancellation**: Exposes a thread-safe `cancel()` hook that abruptly terminates in-flight socket operations when a user hits Ctrl-C or issues a cancellation event.
- **Header Injection**: Applies standard authorization headers (Bearer tokens, custom headers for Anthropic and Gemini, session tokens for Copilot).

## Lessons Learned
- **SSE Buffer Splitting**: Upstream LLM providers occasionally split SSE JSON events across TCP packet boundaries (e.g. half a JSON token in chunk A and the remainder in chunk B). Buffering incomplete lines before JSON parsing prevents JSON decode errors during fast streaming.
- **Clean Socket Teardown on Abort**: When an agent task is cancelled while streaming, explicitly closing the underlying socket forces the HTTP reader to unblock immediately rather than hanging until socket read timeout.
