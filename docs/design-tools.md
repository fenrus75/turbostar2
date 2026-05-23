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

### File Security Manager
Any tool that accesses the filesystem (e.g., read, write, compile) **must** use the `file_security_manager` provided inside the `tool_context` during its Stage 2 `validate_runtime` step.

```cpp
bool my_file_tool::validate_runtime(const agentlib::tool_context& ctx, std::string& out_error) const {
    std::string canonical_path;
    // Strictly validate that the LLM-provided path is allowed for reading
    if (!ctx.fs_security.validate_access(args_.path, agentlib::access_type::read, canonical_path, out_error)) {
        return false;
    }
    // Store the safe, resolved canonical path for execute() to use
    safe_path_ = canonical_path; 
    return true;
}
```
The manager automatically handles `../` directory traversal attacks, resolves symbolic links, enforces workspace root boundaries, and checks against `.agentignore` patterns. Never use a raw path string from the LLM directly in a system call.

## Dealing with JSON Dependencies

To minimize compile times and prevent security bypasses, tool implementation logic (`_entry.cpp`) **must never** `#include <nlohmann/json.hpp>`. The tool's execution phase should only ever operate on native C++ types.

### For Single-File Tools (The Most Common Case)
Most tools (e.g., `fs_read`, `fs_write`, `compile_file`) take exactly one path parameter. You should inherit your validator from `agentlib::single_file_tool_validator`. This base class entirely hides the JSON dependency **and** automatically performs the `file_security_manager` checks during Stage 1.
- You implement `get_required_permission()` to specify if the tool needs read or write access.
- The base class automatically parses the JSON, resolves the path, checks for directory traversal, verifies workspace permissions, and rejects ignores.
- If safe, the execution logic (`llm_tool::execute`) is initialized with a perfectly safe, absolute `std::string safe_path`.
- No JSON headers or explicit security manager checks are needed in either the `_entry.cpp` or `_security.cpp` files.

### For Single-String Tools (Non-File)
For tools taking a single string that is not a file path, inherit from `agentlib::single_string_tool_validator`.
- You implement `validate_string_arg(const std::string& arg, ...)` instead of parsing JSON.
- The execution logic is initialized with a native `std::string`.

### For Multi-Parameter Tools
If a tool requires multiple parameters (e.g., `search_and_replace`), follow the **Marshal Convention**:
1. Define a strongly-typed C++ `struct` for your arguments in the tool's header.
2. In `_security.cpp` *only*, `#include <nlohmann/json.hpp>` and use the `NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE` macro to map the struct.
3. Your validator parses the JSON into this struct, validates the struct, and passes the struct into the `llm_tool`'s constructor.
4. The `_entry.cpp` file remains completely free of JSON dependencies and operates strictly on the native C++ struct.

## Schema Definition Convention

When defining the schema for your tool (e.g., `get_name()`, `get_description()`, `get_parameter_name()`), **inline these simple string-returning methods directly in the class definition within your header file (`.h`)**. 

This keeps the header file acting as built-in documentation for the tool's LLM interface and removes unnecessary boilerplate from the `.cpp` files. Use the `.cpp` files exclusively for the actual C++ validation and execution logic. (If a tool description requires a massive, multi-paragraph prompt, you may optionally move it to `_entry.cpp` using a raw string literal to keep the header clean).

## Security Feedback Loop

If a tool fails validation at either Stage 1 or Stage 2, the framework catches the failure and returns the explicit rejection string directly to the LLM as the tool's result. This allows the LLM to learn why an action was denied and attempt a different approach, rather than simply failing silently or aborting the agent loop.

## Self-Registration

Tools automatically register themselves with the central `tool_registry` at compile-time. There is no central factory file to maintain.

To register a tool, use the `REGISTER_TOOL` macro in the `<tool_name>_security.cpp` file:
```cpp
#include "../../agentlib/tool_registry.h"

REGISTER_TOOL(my_tool_validator_class)
```

## Architectural Guidelines: Specific Git Tools vs Generic Shell Execution

When designing interactions with Git, TurboStar adheres to a strict policy of utilizing dedicated Git tools (e.g., `git_status`, `git_diff_uncommitted`) parsed as Markdown tables, rather than granting the LLM general shell access (`run_shell_command`).

**Security and Sandboxing:** The `file_security_manager` ensures the agent only operates on allowed directories. Providing generic shell access completely bypasses this sandbox. Dedicated tools enforce rigid access controls and require explicit user confirmation for destructive actions (e.g., `--force` pushes).

**Token Efficiency & Parsability:** Raw Git output is verbose, contains ANSI color codes, and includes interactive elements (pagers) that confuse agents. By wrapping Git commands in C++ and outputting curated Markdown tables (e.g., parsing `git status --porcelain`), we supply the LLM with structured, deterministic data that consumes fewer tokens and reduces hallucinations.

**Hybrid Strategy:**
- **Lists & Statuses:** For read-only list operations (`git_status`, `git_branch_list`), use `git --porcelain` or `--format` internally, parse in C++, and return a Markdown table.
- **Content & Diffs:** For operations where table structures don't make sense (`git_diff`, `git_commit` errors), return sanitized raw patch output, strictly enforcing flags like `--no-color`, `--no-pager`, and `--unified=3` to ensure LLM readability.

## Creating a New Tool

1. Create a new subdirectory in `src/tools/` (e.g., `src/tools/my_tool/`).
2. Create a header file `my_tool.h` defining the `llm_tool` and `tool_validator` classes.
3. Create `my_tool_entry.cpp` and implement `get_name()`, `get_description()`, `get_parameters_schema()`, `create_tool()`, and `execute()`.
4. Create `my_tool_security.cpp` and implement `validate_args()`, `validate_runtime()`, and include the `REGISTER_TOOL` macro.
5. Add the new `.cpp` files to `tools_sources` in `src/tools/meson.build`.
