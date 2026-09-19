# Tool Tracer (`tool_tracer`)

## Goals
The `tool_tracer` class provides sequential on-disk execution logging of all LLM tool calls and outputs when enabled via the `--tooltrace` command line option. It captures exact input argument JSON payloads and un-truncated output results into sequential files (`toolcall.0`, `toolcall.1`, `toolcall.2`...) in the current working directory.

## Architecture and Constraints
- **Thread Safety & Ordering**:
  - Increments an atomic counter `std::atomic<size_t> counter_` to assign monotonic sequential IDs across concurrent subagent tool invocations.
  - File I/O is synchronized with `std::mutex mutex_` to prevent interleaved log writes.
- **Diagnostics & Reproducibility**:
  - Writes the tool name, request JSON, and raw result string verbatim.
  - Provides ground-truth input/output data for debugging tool validation failures, prompt injections, and compiler attribution errors.

## Lessons Learned
- **Zero Overhead When Inactive**: Checking `is_enabled()` via an inline atomic check ensures that tool call performance in regular editing sessions is unaffected by tracing infrastructure.
- **Verbatim Output Capturing**: Capturing raw output strings before they are injected into agent interactions ensures that any encoding, tag wrapping, or truncating bugs in the interaction layer can be accurately isolated.
