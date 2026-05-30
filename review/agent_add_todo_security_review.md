# Review of `src/tools/agent_add_todo/agent_add_todo_security.cpp`

## Summary
The file implements a **single‑string validator** for the `agent_add_todo` tool. It performs two main checks before allowing the creation of the tool instance:
1. **Length restriction** – the todo text must be ≤ 1024 characters.
2. **Safety check** – the text must pass `fs_utils::is_safe_for_ui`, which rejects control characters (including ESC) and the DELETE character.

Overall the implementation follows the project's conventions and integrates correctly with the tool‑registry system.

---

## Detailed Findings
| Area | Observation | Recommendation |
|------|-------------|----------------|
| **Correctness** | The validator correctly returns an error message when the length limit is exceeded or when unsafe characters are present. | None needed. |
| **Security** | Uses `fs_utils::is_safe_for_ui` which filters out control characters (0‑31, 127). This prevents injection of terminal escape sequences. | Consider documenting that newline (`\n`) is also rejected (it is a control character). If multiline todos are ever desired, the helper would need to be relaxed. |
| **Error messages** | Clear and specific messages are returned for both failure cases. | No change. |
| **Naming / Consistency** | `agent_add_todo_validator` follows the naming pattern of other validators. | None. |
| **Header Includes** | Includes the necessary headers (`single_string_tool_validator.h`, `tool_registry.h`, `fs_utils.h`, `agent_add_todo.h`). | Could add a comment explaining why `fs_utils.h` is needed (security check). |
| **Style** | Formatting complies with the project’s `.clang-format` (tabs, 8‑space indent). The string concatenation on lines 18‑19 uses a back‑slash for line continuation – acceptable but could be replaced with a raw string literal for readability. | Optional: use a raw string literal, e.g. `R"(Adds a new task …)"`. |
| **Performance** | Validation is O(n) over the string length; negligible for the 1 KB limit. | No change. |
| **Testing** | No unit test is present for this validator. | Add a test case that verifies rejection of overly long input and of a string containing ESC (`\x1b`). |
| **Documentation** | The description string is accurate and matches the user‑facing help. | Ensure the `docs/` files that list tools are regenerated if they pull from this description. |
| **Future‑proofing** | The length limit is hard‑coded; if the UI ever supports longer todos the constant would need to be updated. | Define a `constexpr size_t max_todo_len = 1024;` and use it in both the validator and any UI code that displays the limit. |

---

## Action Items
1. **Add a unit test** for `agent_add_todo_validator` covering:
   - Input longer than 1024 characters → validation fails.
   - Input containing ESC (`\x1b`) → validation fails.
   - Valid input → validation succeeds.
2. **Create a small documentation note** (e.g., in `docs/todo.md` or the tool index) that newline characters are currently disallowed.
3. (Optional) Refactor the description string to a raw literal for readability.
4. (Optional) Introduce a `constexpr` for the max length and replace magic numbers.

---

**Overall Rating:** ✅ *Passes the current security and style requirements.*

## Resolution
1. **Include path stability**: Converted all relative paths to project-root paths in `agent_add_todo_security.cpp`.
2. **Security & Control Characters Test Coverage**: Added comprehensive test cases in `tests/unit/test_tools.cpp` targeting security assertions:
   - Rejection of control characters / escape sequences (`\x1b`).
   - Rejection of inputs exceeding the 1024-character length limit.
   - Verified that read-only protection works correctly in the tool manager framework.

*Prepared by the AI code‑review assistant.*