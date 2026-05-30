# Code Review: `src/agentlib/tool_registry.cpp`

**Reviewer:** LLM AI Assistant
**Date:** 2026-05-29

---

## Overview
The file implements the singleton `tool_registry` used by the agent library to register, enumerate, and invoke tool validators. It provides JSON schema generation for both the generic UI and Gemini‑compatible schemas, as well as security‑focused preparation and execution paths.

## Strengths
- **Clear separation of concerns** – registration, JSON generation, security checks, and execution are each handled in dedicated member functions.
- **Thread‑safe singleton** – `static tool_registry instance;` guarantees safe lazy initialization.
- **Comprehensive security checks** – the `prepare_tool` routine validates arguments, enforces read‑only/plan‑mode restrictions, and performs runtime validation before execution.
- **Consistent JSON metadata** – tool descriptions are annotated with `[Read-Only: …]`, `[State-Modifying: …]` tags, which are useful for UI tooling.
- **Use of modern C++23 features** – structured bindings, `std::move`, and `auto` are used appropriately.

## Issues & Areas for Improvement
| Line(s) | Issue | Impact |
|--------|-------|--------|
| 14‑22 | `register_validator` creates a dummy validator solely to retrieve its name. If the validator construction is heavy or has side‑effects, this incurs unnecessary overhead. | Performance / potential side‑effects |
| 24‑46 & 48‑71 | JSON generation logic is duplicated for the two schema formats. Any change (e.g., new tag wording) must be applied in two places, increasing maintenance risk. | Maintainability |
| 119‑135 | The `try/catch` block around argument parsing and validator creation repeats the same error handling (`"Error parsing tool arguments:"`). The outer `catch` already covers parsing errors; the inner one duplicates logic. | Code duplication |
| 120‑125 | `security_error` is passed by reference to `validate_args`. If the validator does not set it on success, the variable may retain a stale message from a previous iteration. | Potential misleading error messages |
| 130‑131 | In case `create_tool` returns `nullptr`, the error message references “Validation state invalid.” It could be clearer that the factory failed to create the tool. | UX clarity |
| 146‑160 | `execute_tool` forwards the preparation error directly. Consider wrapping it in a more user‑friendly JSON envelope rather than a raw string, for consistency with other UI paths. | Consistency |
| 74‑82 | `is_tool_silent` returns `false` if the tool is not found. Callers may interpret `false` as “not silent” rather than “unknown”. A `std::optional<bool>` or explicit error could be more expressive. | API ergonomics |
| 84‑86 | The function signature spans multiple lines with many parameters, reducing readability. Consider using a struct for the tool‑context parameters. | Readability |

## Recommendations
1. **Avoid dummy validator creation** – store the validator name alongside the factory in the registry, or require the factory to expose a static `name()` method. This removes the need for `auto dummy = factory();`.
2. **Refactor JSON generation** – extract a private helper that builds the JSON object for a given validator and annotation tag. Both `get_tools_json` and `get_gemini_tools_json` can then call this helper.
3. **Consolidate error handling** – merge the two `catch` blocks for argument parsing into a single one; the outer block already captures parsing exceptions.
4. **Initialize `security_error`** – set it to an empty string before each validation call to avoid stale messages.
5. **Improve error messages** – make the failure from `create_tool` explicit (e.g., "Tool factory failed to instantiate tool ‘{}’.")
6. **Consider API ergonomics** – return an `optional<bool>` or a result struct from `is_tool_silent` to differentiate “unknown tool” from “not silent”.
7. **Add unit tests** –
   - Register a mock validator that throws during construction to ensure the registry gracefully handles it.
   - Verify that `prepare_tool` blocks state‑modifying tools when the agent is in read‑only or plan mode.
   - Ensure JSON output contains the correct annotation tags for each tool type.
8. **Documentation** – Add a brief comment block at the top of the file describing the purpose of each public member function, following the project's C++ style guidelines.
9. **Formatting** – Run `clang-format` to ensure indentation aligns with the `.clang-format` settings (tabs, 8‑space columns, etc.).

---

*This review is intended for developers maintaining the `tool_registry` component. No functional changes have been applied yet.*
