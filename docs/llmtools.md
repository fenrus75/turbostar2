# LLM Tool Infrastructure

This document describes the architecture, security model, and implementation guidelines for adding new tools to the LLM agent within Turbostar.

## Directory Structure

All tools are located under `src/tools/`. The core infrastructure that marshals the tool calls from the LLM resides in `src/agentlib/`.

```text
src/
├── agentlib/
│   ├── llm_tool.h            # Base class for tool execution and Stage 2 security
│   ├── tool_validator.h      # Base class for schema definition and Stage 1 security
│   ├── tool_registry.h/.cpp  # Central registry handling self-registration and marshaling
│   ├── tool_context.h        # Wrapper for passing editor state to tools
│   └── security_utils.h/.cpp # Helper library for paths, permissions, etc.
├── tools/
│   ├── meson.build           # Build definitions for all tools
│   ├── <tool_name>/          # Directory for a specific tool
│   │   ├── <tool_name>.h
│   │   ├── <tool_name>_entry.cpp    # Contains schema definition and execution logic
│   │   └── <tool_name>_security.cpp # Contains Stage 1 validation and self-registration
```

## Two-Stage Security Model

To ensure tools are safe and robust, the infrastructure enforces a strict two-stage security model:

### Stage 1: Pre-invocation Validation (`tool_validator`)
Before a tool is even instantiated, a companion validator class parses and validates the JSON arguments requested by the LLM. 
- Implements `validate_args()`.
- Catches issues like missing parameters, invalid formats, or explicit policy violations (e.g., trying to access forbidden paths).
- If validation fails, the tool is never created, and the error reason is returned directly to the LLM so it can correct its mistake.

### Stage 2: Runtime Validation (`llm_tool`)
Once instantiated by the validator (via `create_tool()`), the actual tool object performs a secondary, context-aware validation.
- Implements `validate_runtime()`.
- Validates the operation against the live `tool_context` (e.g., checking if the current document is in a state that allows editing).
- If validation passes, `execute()` is called.

## Dealing with JSON Dependencies

To minimize compile times and prevent security bypasses, tool implementation logic (`_entry.cpp`) **must never** `#include <nlohmann/json.hpp>`. The tool's execution phase should only ever operate on native C++ types.

### For Single-Parameter Tools (The Common Case)
Most tools (e.g., `fs_read`, `compile_file`) take exactly one string parameter (like a file path). You should inherit your validator from `agentlib::single_string_tool_validator`. This base class entirely hides the JSON dependency.
- You implement `validate_string_arg(const std::string& arg, ...)` instead of parsing JSON.
- The execution logic (`llm_tool::execute`) is initialized with a native `std::string`.
- No JSON headers are needed in either the `_entry.cpp` or `_security.cpp` files.

### For Multi-Parameter Tools
If a tool requires multiple parameters (e.g., `search_and_replace`), follow the **Marshal Convention**:
1. Define a strongly-typed C++ `struct` for your arguments in the tool's header.
2. In `_security.cpp` *only*, `#include <nlohmann/json.hpp>` and use the `NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE` macro to map the struct.
3. Your validator parses the JSON into this struct, validates the struct, and passes the struct into the `llm_tool`'s constructor.
4. The `_entry.cpp` file remains completely free of JSON dependencies and operates strictly on the native C++ struct.

## Security Feedback Loop

If a tool fails validation at either Stage 1 or Stage 2, the framework catches the failure and returns the explicit rejection string directly to the LLM as the tool's result. This allows the LLM to learn why an action was denied and attempt a different approach, rather than simply failing silently or aborting the agent loop.

## Self-Registration

Tools automatically register themselves with the central `tool_registry` at compile-time. There is no central factory file to maintain.

To register a tool, use the `REGISTER_TOOL` macro in the `<tool_name>_security.cpp` file:
```cpp
#include "../../agentlib/tool_registry.h"

REGISTER_TOOL(my_tool_validator_class)
```

## Creating a New Tool

1. Create a new subdirectory in `src/tools/` (e.g., `src/tools/my_tool/`).
2. Create a header file `my_tool.h` defining the `llm_tool` and `tool_validator` classes.
3. Create `my_tool_entry.cpp` and implement `get_name()`, `get_description()`, `get_parameters_schema()`, `create_tool()`, and `execute()`.
4. Create `my_tool_security.cpp` and implement `validate_args()`, `validate_runtime()`, and include the `REGISTER_TOOL` macro.
5. Add the new `.cpp` files to `tools_sources` in `src/tools/meson.build`.
