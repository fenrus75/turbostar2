# Purity & Read-Only Agent Security Specification

## 1. Executive Summary & Design Intent
In Turbostar, agents can run with a `read_only = true` property (e.g. `agent_role::summarizer`, code reviewers, security auditors, or research agents). Read-only enforcement guarantees that an agent can analyze, inspect, index, and report on the project without making unapproved modifications to the user's project codebase or git workspace.

To enforce read-only execution cleanly, every tool validator implements `is_pure()`. Before tool execution, `tool_registry::execute_tool()` checks:
```cpp
if (ctx.properties.read_only && !validator->is_pure(args)) {
    return "Security Violation: Agent is in read-only mode and cannot execute state-modifying tool...";
}
```

This specification defines the exact **Purity Rules**, state domain classification, and dynamic URI purity evaluation rules.

---

## 2. Core Rule of Tool Purity

> **Rule of Purity**: A tool invocation is **PURE** (`is_pure() == true`) if and only if it **does NOT modify, delete, or overwrite persistent files in the Project Codebase or Workspace Source Tree**.

### The 4 State Domains Matrix

| Domain | Description | Examples | Purity Status | Read-Only Agent Access |
|---|---|---|---|---|
| **Domain 1: Project Codebase State** | Source files (`.cpp`, `.h`), build manifests (`meson.build`), project configs, and git workspace tree. | `fs_replace_content`, `fs_replace_lines`, `fs_write_file` (workspace), `git_add`, `git_commit`, `hexwrite`, `x86_assemble` | **IMPURE** (`is_pure() == false`) | **BLOCKED** |
| **Domain 2: Agent & Workflow State** | Internal LLM context, loaded skills, active tool families, subagents, timers, status text, and final results. | `activate_tool_family`, `activate_skill`, `report_final_result`, `invoke_subagent`, `send_message`, `wait_for_subagent`, `agent_set_status`, `agent_set_timer`, `agent_mark_episode`, `agent_compress_history` | **PURE** (`is_pure() == true`) | **ALLOWED** |
| **Domain 3: Editor Internal Metadata** | Internal editor data structures (Code Review DB `review.json`), UI error flags, crashdump lists, and symbol caches. | `create_code_review_item`, `update_code_review_item`, `confirm_code_review_item`, `resolve_code_review_item`, `perform_code_review`, `flag_as_error`, `clear_all_errors` | **PURE** (`is_pure() == true`) | **ALLOWED** |
| **Domain 4: Virtual File Systems & Ephemeral Scratchpad** | Transient VFS memory spaces (`images://`), temporary scratch space (`tmp://`), or authorized subagent `output_path`. | `image_crop`, `image_resize`, `image_rotate`, `image_grayscale`, `image_compose`, `image_thumbnail`, `image_import`, `fs_write_file("tmp://...")`, `fs_write_file(output_path)` | **PURE** (`is_pure(args) == true`) | **ALLOWED** |

---

## 3. Dynamic Purity Evaluation (`is_pure(const nlohmann::json &args)`)

Certain tools (like `fs_write_file` or `image_export`) can target either project workspace files or ephemeral VFS scratchpads depending on arguments.

To handle this cleanly, `tool_validator` provides a dynamic overload:
```cpp
class tool_validator {
public:
    virtual bool is_pure() const { return false; }
    virtual bool is_pure(const nlohmann::json & /*args*/) const { return is_pure(); }
};
```

### Path-Based Dynamic Purity Rules:
1. **`tmp://` VFS Scratchpad**: Writing or creating files under `tmp://` (e.g. `tmp://scratch.md`) does not mutate project state $\implies$ `is_pure(args)` returns `true`.
2. **`images://` VFS Memory**: Manipulating or creating images stored in `images://` memory does not mutate project state $\implies$ `is_pure(args)` returns `true`.
3. **Subagent `output_path`**: Single-file write access granted for designated subagent report outputs $\implies$ `is_pure(args)` returns `true`.
4. **Workspace Paths**: Writing or modifying files in the project workspace $\implies$ `is_pure(args)` returns `false`.

---

## 4. Implementation Guidelines for Tool Developers

When implementing a new tool validator:
1. **Does the tool edit `.cpp`, `.h`, or git workspace files?**
   $\to$ Return `false` for `is_pure()`.
2. **Does the tool manage agent workflow, subagents, timers, or report results?**
   $\to$ Return `true` for `is_pure()`.
3. **Does the tool update code review database (`create_code_review_item`) or editor diagnostic flags?**
   $\to$ Return `true` for `is_pure()`.
4. **Does the tool perform in-memory image processing (`images://`)?**
   $\to$ Return `true` for `is_pure()`.
5. **Does the tool write files where target path varies (`fs_write_file`)?**
   $\to$ Override `is_pure(const nlohmann::json &args)` to check if target is a scratch URI (`tmp://`, `images://`) or `output_path`.
