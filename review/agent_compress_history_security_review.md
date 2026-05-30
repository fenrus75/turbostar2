# Code Review: `src/tools/agent_compress_history/agent_compress_history_security.cpp`

**Overall Assessment**
- The implementation is concise and follows the existing pattern for tool validators in the code‑base.
- JSON schema definition, argument validation, and tool creation are clearly separated.
- The file compiles cleanly with the current tool‑registry infrastructure.

---

## 1. Code Structure & Style
- **Header Includes**: Uses relative includes (`"../../agentlib/tool_registry.h"`). This is acceptable given the current project layout, but consider using a project‑wide include path (e.g., `#include <agentlib/tool_registry.h>`) to avoid brittle relative paths.
- **Namespace**: All symbols are correctly placed inside `namespace tools`.
- **Naming**: The class `agent_compress_history_validator` follows the existing naming convention for validators.
- **Formatting**: The file adheres to the project's `.clang-format` settings (tab‑based indentation, 8‑space width).

---

## 2. JSON Schema Definition
- The schema correctly marks `title` and `summary` as required.
- All optional fields (`tags`, `target_episode_id`, `include_all_prior`) are defined with appropriate types and descriptions.
- **Potential Improvement**:
  - Add `maxLength` constraints for `title` and `summary` to prevent excessively large strings that could degrade UI rendering or cause memory pressure.
  - Provide an `enum` or pattern for `target_episode_id` if there is a known format (e.g., `^episode_\d+$`).

---

## 3. Argument Validation (`validate_args_impl`)
- Deserialization is performed with `raw_json.get<agent_compress_history_args>()` inside a `try/catch`. This correctly propagates parsing errors.
- **Security Considerations**:
  - The validator does **not** perform any further sanity checks beyond JSON parsing. If an attacker can influence the JSON payload, they could:
    - Supply a very large `summary` string, leading to high memory usage when the pointer is injected into the context.
    - Provide a malicious `target_episode_id` that references a non‑existent or privileged episode, potentially causing undefined behaviour in downstream code.
  - **Recommendation**: Add explicit checks after deserialization:
    ```cpp
    if (args_.title.size() > 200) { out_error = "Title too long"; return false; }
    if (args_.summary.size() > 2000) { out_error = "Summary too long"; return false; }
    // optionally verify target_episode_id format
    ```
- The error message concatenates the exception message directly. This is safe because `std::exception::what()` is under our control, but consider sanitising or limiting the length of `out_error` to avoid overly verbose messages.

---

## 4. Tool Creation (`create_tool_impl`)
- Returns a `std::unique_ptr<agentlib::llm_tool>` constructed with `args_`. This is consistent with other validators.
- Ensure that `agent_compress_history_tool` correctly handles the `include_all_prior` flag; otherwise, the flag could be ignored silently.

---

## 5. Registration Macro
- The `REGISTER_TOOL(agent_compress_history_validator)` macro registers the validator with the tool registry. No issues observed.

---

## 6. Missing Tests
- The repository's testing policy requires a failing test before fixing a bug. There is currently **no unit test** for this validator.
- **Action Items**:
  1. Add a test case in `tests/` that supplies a valid JSON payload and expects successful validation.
  2. Add a second test case with an invalid payload (e.g., missing `summary`) and verify that validation fails with the appropriate error message.

---

## 7. Documentation Updates
- The review does **not** touch any files in `docs/`, respecting the request.
- However, consider updating `docs/todo.md` to note the addition of the new test cases for the compress‑history tool.

---

## 8. Summary of Recommendations
| Area | Recommendation |
|------|----------------|
| Include Paths | Prefer project‑wide include paths over relative ones. |
| JSON Schema | Add `maxLength` constraints for `title`/`summary`; optionally a pattern for `target_episode_id`. |
| Validation Logic | Perform explicit size/format checks after deserialization to guard against resource abuse. |
| Error Messages | Trim or limit length of `out_error` for robustness. |
| Tests | Add unit tests for both success and failure scenarios. |
| Documentation | Log new tests in `docs/todo.md`. |

---

**Conclusion**
The file is well‑structured and integrates cleanly with the existing tool framework. Implementing the above security‑hardening checks and adding comprehensive tests will improve robustness and maintainability.

## Resolution
1. **Security boundaries implemented**:
   - Limit `title` length to <= 200 characters.
   - Limit `summary` length to <= 2000 characters.
   - Limit `target_episode_id` length to <= 256 characters.
   - Used `fs_utils::is_safe_for_ui` to scan both the `title` and `summary` strings, rejecting unsafe terminal control sequences / ESC inputs.
2. **Include clean-ups**: Converted includes to use project-root style.
3. **Comprehensive unit testing**: Implemented a standalone unit test (`tests/unit/test_agent_compress_history.cpp`) that explicitly checks all of these length limits, control character injections, and agent read-only protections. Added it to the main `meson.build` test registry.

*Prepared by the AI code‑review assistant.*
