## activate_skill

- **Security Review**: The tool validates that the `name` argument is non‑empty and checks that the requested skill exists in the registered skill manager before proceeding. It uses only in‑process lookups; no external process execution or file system access occurs. Errors are reported via clear messages. No obvious security concerns.

---
## agent_add_todo

- **Security Review**: Validates that the todo text does not exceed 1024 characters and calls `fs_utils::is_safe_for_ui` to reject control characters or escape sequences, preventing injection attacks via UI. No filesystem writes beyond adding a task to the internal todo list.

---
