# Code Review – `src/tools/agent_complete_todo/agent_complete_todo_entry.cpp`

**Overall impression**
- The implementation is concise and follows the existing tool‑pattern used throughout the project.
- It correctly validates runtime pre‑conditions and forwards the request to the active agent.
- Naming, indentation and overall style conform to the repository’s `.clang‑format` (tabs, 8‑space width, Linux brace style).

---

## Things that are done well
1. **Validation logic** – `validate_runtime` checks both the presence of an active agent and that the search string is non‑empty, providing clear error messages.
2. **Error propagation** – The `execute` method forwards any error from `mark_todo_complete` to the caller via `set_failure`, preserving the original diagnostic.
3. **Use of move semantics** – The constructor moves the incoming `text_match` into the member, avoiding an unnecessary copy.
4. **Consistent API usage** – The tool correctly inherits from `llm_tool_action` and uses the base‑class helpers `set_success` / `set_failure`.

---

## Minor issues & recommendations
| Line | Issue | Recommendation |
|------|-------|----------------|
| 1‑2  | The file includes a relative path (`"../../agentlib/ai_agent.h"`). This works but is brittle if the directory layout changes. Prefer an include that mirrors the project’s include directories (e.g., `#include "agentlib/ai_agent.h"`). |
| 1‑2  | Missing explicit standard library includes (`<string>` and `<utility>` for `std::move`). They are currently pulled transitively via other headers, but adding them makes the file self‑contained and improves compile‑time robustness. |
| 25‑33| The returned messages are raw strings. For consistency with the rest of the codebase (which prefers `std::format` where possible) consider using `std::format("Task marked complete.")` and `std::format("Error: {}", err)`. |
| 36   | Ensure the file ends with a newline character – some tools flag missing EOF newline as a style warning. |
| N/A  | Add a brief documentation comment (Doxygen style) above the class definition (in the corresponding header) describing the purpose of the tool. This improves discoverability for future developers. |

---

## Suggested changes (minimal diff)
```diff
@@
-#include "../../agentlib/ai_agent.h"
-#include "agent_complete_todo.h"
+#include "agentlib/ai_agent.h"
+#include "agent_complete_todo.h"
+#include <string>
+#include <utility>
@@
-    std::string err;
-    if (ctx.active_agent->mark_todo_complete(text_match_, err)) {
-        set_success(ctx);
-        return "Task marked complete.";
-    }
-    set_failure(ctx, err);
-    return "Error: " + err;
+    std::string err;
+    if (ctx.active_agent->mark_todo_complete(text_match_, err)) {
+        set_success(ctx);
+        return std::format("Task marked complete.");
+    }
+    set_failure(ctx, err);
+    return std::format("Error: {}", err);
``` 
*(Only the lines that need modification are shown; the rest of the file stays unchanged.)*

---

## Next steps
- Apply the minimal diff above.
- Run the test suite (`meson test -j2 -C build`) to ensure no regressions.
- Commit the changes with a conventional commit message, e.g. `fix(tools): improve robustness of agent_complete_todo_entry`.

## Resolution
1. **Include path stability**: Updated relative path includes in both `agent_complete_todo_entry.cpp` and `agent_complete_todo_security.cpp` to use project-root paths.
2. **Explicit Standard Library Includes**: Added `#include <format>`, `#include <string>`, and `#include <utility>` explicitly.
3. **Execution Robustness & std::format**: Replaced raw string concatenations with `std::format` in the returned success/error string payloads in `execute()`.
4. **Security Validation**:
   - Implemented length checks (limit 1024 characters) in `agent_complete_todo_security.cpp`.
   - Added character validation with `fs_utils::is_safe_for_ui(arg)` to filter out control and escape sequences.
   - Enforced runtime `is_read_only()` checks in both the registry preparation check and the tool's `validate_runtime`.
5. **Standalone Unit Test**: Created a dedicated unit test file `tests/unit/test_agent_complete_todo.cpp` checking all edge cases (successful completion, empty text failure, length limit validation, control character blocking, read-only validation).
6. **Build System Registration**: Added `test_agent_complete_todo` (and also `test_agent_add_todo`) as standalone unit tests in `meson.build`.

*This review is placed in the `review/` directory as requested.*