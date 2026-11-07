# llm_types

## Goals and Constraints
`llm_types.h` and `llm_types.cpp` provide the core data structures used for LLM interaction and agent conversation turns:
- `function_call`: Container for LLM tool call name and JSON arguments string.
- `tool_call`: Full tool invocation payload including ID, type, function details, and optional signature.
- `message`: Role-based conversation item (system, user, assistant, tool) with reasoning and temporal tracking fields.
- `llm_usage`: Token accounting data (prompt, completion, total, cached tokens).
- `llm_chat_response` and `chat_delta`: Container types for completed responses and streaming deltas.
- `normalize_tool_call`: Maps legacy tool aliases and schema arguments to official Turbostar tool definitions.

## Constraints
- Serialization logic (`to_json`, `from_json`) and large normalization mappings (`normalize_tool_call`) MUST reside in `llm_types.cpp` to prevent heavy header bloat and avoid dragging filesystem/utility headers into every agent component.
- Keep `llm_types.h` minimal and free of `#include "fs_utils.h"` to prevent ripple recompilations across the entire test suite when filesystem utilities are updated.

## Lessons Learned
- Inline implementation of JSON serialization and tool normalization in `llm_types.h` directly included `fs_utils.h` (and transitively `fs_basics.h`), causing touches to basic utilities to force recompilation of hundreds of agent and test translation units. Out-of-lining these implementations into `llm_types.cpp` decouples the agent header graph from core filesystem utilities.
